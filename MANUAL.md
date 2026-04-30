# zwaved Operator Manual — D-Bus Interface

This document covers operating the running `zwaved` daemon over its D-Bus
interface to add and remove Z-Wave nodes. It targets the **system bus**.

- **Bus name:** `com.tiunda.ZWaved`
- **Object path:** `/com/tiunda/ZWaved`
- **Interface:** `com.tiunda.ZWaved1`

A future ubus transport will mirror these method/signal names; everything
below applies regardless of which transport is in use.

## 1. Prerequisites

### Install the system bus policy

The policy file ships in the repository at `dbus/com.tiunda.ZWaved.conf`.
Install it once per host so non-root callers can reach the service:

```bash
sudo install -m 0644 dbus/com.tiunda.ZWaved.conf /etc/dbus-1/system.d/
sudo systemctl reload dbus
```

### Run the daemon

`zwaved` must run as root (or as a user permitted to own the bus name)
and have the Z-Wave dongle plugged in:

```bash
sudo ./cmake-build-gnu/zwaved
```

### Verify the service is up

```bash
busctl --system list | grep ZWaved
busctl --system introspect com.tiunda.ZWaved /com/tiunda/ZWaved
```

The introspection should list four methods (`AddNode`, `StopAddNode`,
`RemoveNode`, `StopRemoveNode`) and three signals (`NodeInclusionStatus`,
`NodeExclusionStatus`, `DongleStatus`).

### Always monitor signals in another terminal

The status you care about arrives as **signals**, not method return
values. Open a second shell and run:

```bash
busctl --system monitor com.tiunda.ZWaved
```

### DongleStatus signal

Independently of any method call, `zwaved` broadcasts `DongleStatus(b s)`
whenever the Z-Wave dongle is plugged in or unplugged:

| Parameter | Type | Meaning |
|-----------|------|---------|
| `connected` | `b` (BOOLEAN) | `true` when the TTY has been discovered; `false` on detach |
| `ttyPath` | `s` (STRING) | TTY path (e.g. `/dev/ttyACM0`) when connected; empty otherwise |

The signal appears in the same `busctl --system monitor` stream as the
inclusion/exclusion signals. It is fire-and-forget — a client that
connects after the dongle is already attached will not receive a
historical event; query the daemon's stdout (`Z-Wave dongle inserted:`)
or wait for the next hot-plug to determine current state.

## 2. Method reference

| Method | Signature | Purpose |
|--------|-----------|---------|
| `AddNode` | `y y y ay ay` (mode, flags, sessionId, nwiHomeId, authHomeId) | Start an inclusion of any/SmartStart variant |
| `StopAddNode` | `y` (sessionId) | Send Mode `0x05` to stop an in-progress inclusion |
| `RemoveNode` | `y y y` (mode, flags, sessionId) | Start an exclusion |
| `StopRemoveNode` | `y` (sessionId) | Send Mode `0x05` to stop an in-progress exclusion |

`y` = `BYTE` (uint8), `q` = `UINT16`, `ay` = array of bytes.

`sessionId` is an opaque 1-byte token chosen by the caller; it is echoed
back in every signal so multiple callers can correlate their work.
**A `sessionId` of `0` instructs the dongle not to emit callbacks** — pick
any non-zero value (1..255) for normal use.

## 3. Adding a node — Classic (Mode `0x01`)

Use Mode `0x01` ("Add any node") for the everyday inclusion flow. Pass
empty arrays for both home-IDs.

```bash
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 AddNode yyyayay 1 0 42 0 0
```

`mode=1`, `flags=0`, `sessionId=42`, both home-ID arrays empty.

After issuing the call, **press the inclusion button on the new node**.
Watch the monitor terminal for `NodeInclusionStatus` signals. The expected
status progression is:

| Status | Meaning |
|--------|---------|
| `0x01` | Network Inclusion Started (controller is listening) |
| `0x02` | Node found |
| `0x03` | Inclusion ongoing — End Node |
| `0x04` | Inclusion ongoing — Controller Node |
| `0x05` | Protocol part complete; neighbor discovery |
| `0x06` | Inclusion completed — call `StopAddNode` to return to idle |

Once you see `0x06`, stop the controller:

```bash
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 StopAddNode y 42
```

## 4. Adding a node — SmartStart Listen (Mode `0x09`)

SmartStart Listen tells the controller to keep listening for SmartStart
prime commands; nothing is included until a matching device announces
itself. Home-ID arrays should be **empty**:

```bash
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 AddNode yyyayay 9 0 100 0 0
```

The controller stays in listen mode until you call `StopAddNode 100`.
Devices that announce themselves with a known DSK will then transition
the session through the same status progression as classic inclusion.

## 5. Adding a node — SmartStart Include (Mode `0x08`)

Mode `0x08` performs a targeted SmartStart inclusion. You must supply
both `nwiHomeId` and `authHomeId` derived from the device's DSK (4 bytes
each). Encode them as comma-separated bytes after `5` (the array length):

```bash
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 AddNode yyyayay \
    8 0 200 \
    4 0xAA 0xBB 0xCC 0xDD \
    4 0x11 0x22 0x33 0x44
```

`mode=8`, `flags=0`, `sessionId=200`, NWI HomeID `AA BB CC DD`, Auth
HomeID `11 22 33 44`. Status progression matches classic inclusion;
finish with `StopAddNode 200`.

## 6. Stopping inclusion (and stopping replication)

`StopAddNode` sends Mode `0x05`. To stop *controller replication*
specifically (Mode `0x06`), call `AddNode` with `mode=6`:

```bash
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 AddNode yyyayay 6 0 42 0 0
```

## 7. Removing a node — Network exclusion (Mode `0x01`)

```bash
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 RemoveNode yyy 1 0 43
```

`mode=1`, `flags=0`, `sessionId=43`. **Press the exclusion button on
the target device.** Expected `NodeExclusionStatus` progression:

| Status | Meaning |
|--------|---------|
| `0x01` | Network Exclusion Started |
| `0x02` | Node found |
| `0x03` | Exclusion ongoing — End Node |
| `0x04` | Exclusion ongoing — Controller Node |
| `0x06` | Exclusion completed — call `StopRemoveNode` |

Then:

```bash
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 StopRemoveNode y 43
```

## 8. Flag-byte cheat sheet

The `flags` byte combines optional bits with the low-nibble Mode value
that the controller uses internally. zwaved sets the Mode field for you
from the `mode` argument; you set the optional bits via `flags`.

### `AddNode` flags

| Bit | Name | Meaning |
|-----|------|---------|
| 7   | Power     | Use high power for inclusion |
| 6   | NWI       | Network Wide Inclusion |
| 5   | Protocol  | `1` = Z-Wave Long Range, `0` = Z-Wave classic |
| 4   | SFLND     | Skip FL nodes during neighbor discovery |
| 3..0 | Mode    | Echoed from the `mode` argument by zwaved |

Examples:

- `flags = 0x00` (decimal 0): plain classic, no extras
- `flags = 0x40` (decimal 64): NWI bit set
- `flags = 0xC0` (decimal 192): Power + NWI

### `RemoveNode` flags

| Bit | Name | Meaning |
|-----|------|---------|
| 7   | Power | Use high power for exclusion |
| 6   | NWE   | Network Wide Exclusion |
| 5   | Reserved | Must be 0 |
| 3..0 | Mode | Echoed from `mode` |

## 9. Status-byte reference

Both inclusion and exclusion use the same low values for the early
states; the diverging meanings of higher values are summarized here.
Source: spec Tables 4.124 and 4.134.

| Status | Inclusion meaning | Exclusion meaning |
|--------|-------------------|-------------------|
| `0x01` | Network Inclusion Started | Network Exclusion Started |
| `0x02` | Node found | Node found |
| `0x03` | Inclusion ongoing — End Node | Exclusion ongoing — End Node |
| `0x04` | Inclusion ongoing — Controller Node | Exclusion ongoing — Controller Node |
| `0x05` | Inclusion complete (protocol part) | Reserved |
| `0x06` | Inclusion completed | Exclusion completed |
| `0x07` | Inclusion failed | Exclusion failed |
| `0x0B` | Neighbors Discovery skipping FL nodes done | — |
| `0x23` | Not primary controller | Not primary controller |

## 10. Troubleshooting

- **`Failed to call: Access denied`** — the policy file isn't installed
  (or hasn't been reloaded). See §1.
- **`No such name "com.tiunda.ZWaved"`** — the daemon isn't running, or
  failed to acquire the bus name. Check `journalctl` / stdout for
  `[DBusBackend] failed to acquire system bus name`.
- **Method call returns OK but no signals arrive** — most often the
  dongle was unplugged or the protocol thread is in `awaitDevicePath`.
  Check daemon stdout for `Z-Wave dongle inserted:` and `[SerialPort]
  opened`.
- **Signals stop at `0x07` (Inclusion/Exclusion failed)** — typical for
  a node that didn't press its inclusion button in time. Send the
  matching `Stop*` and try again with a fresh `sessionId`.
- **`0x23` (Not primary)** — this controller isn't the primary on the
  network. Add/Remove cannot be initiated from a secondary controller.
- **Session ID `0` produces no signals** — by spec; pick a non-zero
  `sessionId` (1..255).

## 11. Future: ubus

A second backend implementing the same methods/signals over OpenWrt's
ubus is on the roadmap. The CMake cache option `ZWAVED_EXTERNAL_API`
already accepts `ubus` and `both`, but only `dbus` is implemented today.
