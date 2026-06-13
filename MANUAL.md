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

The introspection should list twenty-two methods (`AddNode`, `StopAddNode`,
`RemoveNode`, `StopRemoveNode`, `RemoveFailedNode`, `SetSwitchBinary`,
`GetSwitchBinary`, `SetBasic`, `GetBasic`, `SetMultilevelSwitch`,
`GetMultilevelSwitch`, `GetNodes`, `GetDongleInfo`, `GetInitData`,
`SetAssociation`, `RemoveAssociation`, `GetAssociation`,
`GetAssociationGroupings`, `SetMultichannelAssociation`,
`RemoveMultichannelAssociation`, `GetMultichannelAssociation`,
`GetMultichannelAssociationGroupings`) and
twelve signals (`NodeInclusionStatus`, `NodeExclusionStatus`,
`DongleStatus`, `DongleInfo`, `InitData`, `SendDataStatus`,
`ApplicationCommand`, `SwitchBinaryReport`, `SwitchMultilevelReport`,
`AssociationReport`, `AssociationGroupingsReport`, `RemoveFailedNodeStatus`).

### Always monitor signals in another terminal

The status you care about arrives as **signals**, not method return
values. Open a second shell and run:

```bash
busctl --system monitor com.tiunda.ZWaved
```

### DongleStatus signal

Independently of any method call, `zwaved` broadcasts `DongleStatus(b s)`
whenever the Z-Wave dongle is plugged in or unplugged:

| Parameter   | Type          | Meaning                                                        |
|-------------|---------------|----------------------------------------------------------------|
| `connected` | `b` (BOOLEAN) | `true` when the TTY has been discovered; `false` on detach     |
| `ttyPath`   | `s` (STRING)  | TTY path (e.g. `/dev/ttyACM0`) when connected; empty otherwise |

The signal appears in the same `busctl --system monitor` stream as the
inclusion/exclusion signals. It is fire-and-forget — a client that
connects after the dongle is already attached will not receive a
historical event; query the daemon's stdout (`Z-Wave dongle inserted:`)
or wait for the next hot-plug to determine current state.

## 2. Method reference

| Method                                | Signature                                                                                                                                          | Purpose                                                                                                                                                                      |
|---------------------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------|------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `AddNode`                             | `y y y ay ay` (mode, flags, sessionId, nwiHomeId, authHomeId)                                                                                      | Start an inclusion of any/SmartStart variant                                                                                                                                 |
| `StopAddNode`                         | `y` (sessionId)                                                                                                                                    | Send Mode `0x05` to stop an in-progress inclusion                                                                                                                            |
| `RemoveNode`                          | `y y y` (mode, flags, sessionId)                                                                                                                   | Start an exclusion                                                                                                                                                           |
| `StopRemoveNode`                      | `y` (sessionId)                                                                                                                                    | Send Mode `0x05` to stop an in-progress exclusion                                                                                                                            |
| `RemoveFailedNode`                    | `y y` (nodeId, sessionId)                                                                                                                          | Drive `FUNC_ID_ZW_REMOVE_FAILED_NODE_ID` (0x61) for a node that has stopped responding; emits `RemoveFailedNodeStatus` for both the immediate response and the final outcome |
| `GetVersion`                          | `→ (s s)` (semver, gitDescribe)                                                                                                                    | Return the daemon's own version (semver bumped manually in `project()`, plus `git describe --tags --dirty --always` from build time)                                         |
| `GetNetworkStatus`                    | `→ (b s s y u b y y t)` (dongleConnected, ttyPath, homeId, controllerNodeId, nodeCount, sessionActive, sessionCommandId, sessionId, uptimeSeconds) | Aggregate snapshot of the daemon's view of the network: dongle connection, home ID, included-node count, in-flight inclusion/exclusion session, daemon uptime                |
| `GetDaemonError`                      | `→ (y s y s)` (severity, source, code, message)                                                                                                    | The latest operator-visible error from the retained `DaemonError` feed; empty `message` means "no current problem". Lets a client connecting after a failure see it (the `DaemonError` signal isn't replayed to late subscribers) |
| `SetSwitchBinary`                     | `y b y` (nodeId, on, callbackId)                                                                                                                   | Send a Binary Switch SET (CC 0x25) to a node; completion arrives as `SendDataStatus(callbackId, txStatus)`                                                                   |
| `GetSwitchBinary`                     | `y y` (nodeId, callbackId)                                                                                                                         | Send a Binary Switch GET; the node's reply lands as a typed `SwitchBinaryReport(sourceNodeId, state)` signal alongside the raw `ApplicationCommand`                          |
| `SetBasic`                            | `y y y` (nodeId, value, callbackId)                                                                                                                | Send a Basic SET (CC 0x20) — `value=0` off, `value=0xFF` on, `value=1..99` (`0x01..0x63`) dimmer level. Universal fallback for devices without a specific CC                 |
| `GetBasic`                            | `y y` (nodeId, callbackId)                                                                                                                         | Send a Basic GET; the node's reply lands as a raw `ApplicationCommand` signal carrying the Basic Report (`ccData[0] == 0x20`, `ccData[1] == 0x03`)                           |
| `SetMultilevelSwitch`                 | `y y y y` (nodeId, value, duration, callbackId)                                                                                                    | Send a Multilevel Switch SET (CC 0x26) — `value=0` off, `value=1..99` (`0x01..0x63`) dimmer level, `value=0xFF` restore-last. `duration=0` instant, `0xFF` factory default   |
| `GetMultilevelSwitch`                 | `y y` (nodeId, callbackId)                                                                                                                         | Send a Multilevel Switch GET; the node's reply lands as a typed `SwitchMultilevelReport(sourceNodeId, currentValue, targetValue, duration)` signal alongside the raw `ApplicationCommand` |
| `GetNodes`                            | `→ a(yyyyay)` (array of nodeId, basic, generic, specific, ccBytes)                                                                                 | Return the in-memory list of currently-included nodes                                                                                                                        |
| `GetDongleInfo`                       | `→ (s y ay y)` (libraryVersion, libraryType, homeId, controllerNodeId)                                                                             | Return the dongle introspection captured when the serial port opened                                                                                                         |
| `GetInitData`                         | `→ (y y ay y y)` (serialApiVersion, capabilities, nodeIds, chipType, chipVersion)                                                                  | Return the SERIAL_API_GET_INIT_DATA response captured at startup; `nodeIds` is the expanded node bitmap                                                                      |
| `SetAssociation`                      | `y y ay y` (nodeId, groupId, members, callbackId)                                                                                                  | Add `members` to `groupId` on `nodeId`'s association table (CC 0x85 cmd 0x01)                                                                                                |
| `RemoveAssociation`                   | `y y ay y` (nodeId, groupId, members, callbackId)                                                                                                  | Remove `members` from `groupId` (empty `members` means *all*)                                                                                                                |
| `GetAssociation`                      | `y y y` (nodeId, groupId, callbackId)                                                                                                              | Query the current members of `groupId`; result arrives as `AssociationReport`                                                                                                |
| `GetAssociationGroupings`             | `y y` (nodeId, callbackId)                                                                                                                         | Query how many association groups `nodeId` exposes; result arrives as `AssociationGroupingsReport`                                                                           |
| `SetMultichannelAssociation`          | `y y ay a(yy) y` (nodeId, groupId, nodeMembers, endpointMembers, callbackId)                                                                       | Add `nodeMembers` and `(nodeId,endpoint)` pairs to `groupId` (CC 0x8E cmd 0x01)                                                                                              |
| `RemoveMultichannelAssociation`       | `y y ay a(yy) y` (nodeId, groupId, nodeMembers, endpointMembers, callbackId)                                                                       | Remove members from `groupId`; both arrays empty means *all*                                                                                                                 |
| `GetMultichannelAssociation`          | `y y y` (nodeId, groupId, callbackId)                                                                                                              | Query members; result arrives as `ApplicationCommand` carrying a Multi Channel Association REPORT (CC 0x8E cmd 0x03)                                                         |
| `GetMultichannelAssociationGroupings` | `y y` (nodeId, callbackId)                                                                                                                         | Query supported groupings; result arrives as `ApplicationCommand` carrying a Multi Channel Association GROUPINGS REPORT (CC 0x8E cmd 0x06)                                   |

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

| Status | Meaning                                                    |
|--------|------------------------------------------------------------|
| `0x01` | Network Inclusion Started (controller is listening)        |
| `0x02` | Node found                                                 |
| `0x03` | Inclusion ongoing — End Node                               |
| `0x04` | Inclusion ongoing — Controller Node                        |
| `0x05` | Protocol part complete; neighbor discovery                 |
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

| Status | Meaning                                     |
|--------|---------------------------------------------|
| `0x01` | Network Exclusion Started                   |
| `0x02` | Node found                                  |
| `0x03` | Exclusion ongoing — End Node                |
| `0x04` | Exclusion ongoing — Controller Node         |
| `0x06` | Exclusion completed — call `StopRemoveNode` |

Then:

```bash
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 StopRemoveNode y 43
```

## 7b. Removing a failed node

When a node stops responding, the controller adds it to its internal
failed-node list. Use `RemoveFailedNode` to evict it from the routing
table without needing the node to participate in a normal exclusion
(which would require the node to be alive).

```bash
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 RemoveFailedNode yy 11 7
```

`nodeId=11`, `sessionId=7`. Two `RemoveFailedNodeStatus(y y y y)`
signals follow, both echoing the same `nodeId` / `sessionId`:

| `phase` | Meaning                                                         | `status` decoded against                                                                                |
|---------|-----------------------------------------------------------------|---------------------------------------------------------------------------------------------------------|
| `0`     | Response — whether the dongle accepted the request              | `STARTED=0x00`, `NOT_PRIMARY=0x02`, `NO_CALLBACK=0x04`, `NODE_NOT_FOUND=0x08`, `BUSY=0x10`, `FAIL=0x20` |
| `1`     | Result — final outcome (only emitted if response was `STARTED`) | `NODE_OK=0x00` (the node responded — not a failed node), `REMOVED=0x01`, `NOT_REMOVED=0x02`             |

A `phase=1, status=0x01` (REMOVED) success automatically trims the node
from the registry; subsequent `GetNodes` calls will not include it.

The dongle answers `NODE_NOT_FOUND` (`phase=0, status=0x08`) if the node
is not on its failed-node list — typically because the node is still
responding or has never been included. To force a node onto the failed
list, send a regular `SetSwitchBinary` (or any SendData) addressed to
the unresponsive node and watch for a `SendDataStatus` with `txStatus
= 0x01` (NO_ACK); the dongle promotes the node to "failed" after a few
such failures.

## 8. Flag-byte cheat sheet

The `flags` byte combines optional bits with the low-nibble Mode value
that the controller uses internally. zwaved sets the Mode field for you
from the `mode` argument; you set the optional bits via `flags`.

### `AddNode` flags

| Bit  | Name     | Meaning                                       |
|------|----------|-----------------------------------------------|
| 7    | Power    | Use high power for inclusion                  |
| 6    | NWI      | Network Wide Inclusion                        |
| 5    | Protocol | `1` = Z-Wave Long Range, `0` = Z-Wave classic |
| 4    | SFLND    | Skip FL nodes during neighbor discovery       |
| 3..0 | Mode     | Echoed from the `mode` argument by zwaved     |

Examples:

- `flags = 0x00` (decimal 0): plain classic, no extras
- `flags = 0x40` (decimal 64): NWI bit set
- `flags = 0xC0` (decimal 192): Power + NWI

### `RemoveNode` flags

| Bit  | Name     | Meaning                      |
|------|----------|------------------------------|
| 7    | Power    | Use high power for exclusion |
| 6    | NWE      | Network Wide Exclusion       |
| 5    | Reserved | Must be 0                    |
| 3..0 | Mode     | Echoed from `mode`           |

## 9. Status-byte reference

Both inclusion and exclusion use the same low values for the early
states; the diverging meanings of higher values are summarized here.
Source: spec Tables 4.124 and 4.134.

| Status | Inclusion meaning                          | Exclusion meaning                   |
|--------|--------------------------------------------|-------------------------------------|
| `0x01` | Network Inclusion Started                  | Network Exclusion Started           |
| `0x02` | Node found                                 | Node found                          |
| `0x03` | Inclusion ongoing — End Node               | Exclusion ongoing — End Node        |
| `0x04` | Inclusion ongoing — Controller Node        | Exclusion ongoing — Controller Node |
| `0x05` | Inclusion complete (protocol part)         | Reserved                            |
| `0x06` | Inclusion completed                        | Exclusion completed                 |
| `0x07` | Inclusion failed                           | Exclusion failed                    |
| `0x0B` | Neighbors Discovery skipping FL nodes done | —                                   |
| `0x23` | Not primary controller                     | Not primary controller              |

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

## 11. Driving a Binary Switch (CC 0x25)

`SetSwitchBinary` sends a Binary Switch SET (Command Class `0x25`,
command `0x01`) to an already-included node. `nodeId` is the 1-byte
node ID returned by the inclusion flow; `on=true` translates to value
`0xFF`, `on=false` to `0x00`. `callbackId` is an opaque 1-byte token
chosen by the caller and echoed back in the matching `SendDataStatus`
signal.

```bash
# Turn node 5 on, with callback id 7:
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 SetSwitchBinary yby 5 true 7
```

Watch the monitor terminal for:

```
SendDataStatus y y 7 0
```

The first byte is the echoed `callbackId`, the second is the
`txStatus`:

| `txStatus` | Meaning                           |
|------------|-----------------------------------|
| `0x00`     | Transmit complete OK (node ACK'd) |
| `0x01`     | No ACK from destination           |
| `0x02`     | Transmit failed                   |
| `0x03`     | Routing not idle                  |
| `0x04`     | No route to destination           |
| `0x05`     | Verified delivery                 |

A `0x01` (no ACK) usually means the node is asleep or out of range.
A `0x02` typically means the dongle accepted the request but
transmission failed somewhere in the network.

To read the current state, call `GetSwitchBinary`. The node's reply
arrives asynchronously as both a typed `SwitchBinaryReport(sourceNodeId,
state)` signal (`state` = `0` Off, `1` On, `2` Unknown) and a raw
`ApplicationCommand` carrying the same bytes — see §12. The same typed
signal also fires whenever the node sends an unsolicited Report after a
manual toggle.

```bash
# Query node 5's current Binary Switch state, callback id 9:
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 GetSwitchBinary yy 5 9
```

## 11b. Driving a Basic value (CC 0x20)

The Basic Command Class is the universal fallback — devices that don't
expose a more specific CC for their primary behaviour will still
respond to `Basic SET` and `Basic GET`. A binary switch interprets it
as on/off, a dimmer as a 0–99 level, a thermostat as a setpoint hint,
etc. When in doubt about a node's primary CC, Basic is the safe
starting point.

```bash
# Turn node 5 fully on (0xFF) with callback id 7:
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 SetBasic yyy 5 0xFF 7

# Set node 5 to 50 % (decimal 50 → 0x32):
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 SetBasic yyy 5 50 8

# Read the current value (Report arrives as ApplicationCommand):
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 GetBasic yy 5 9
```

The `Set` calls complete with the same `SendDataStatus(callbackId,
txStatus)` table as `SetSwitchBinary`. The `Get` reply doesn't get a
typed signal today — clients filter the existing `ApplicationCommand`
stream on `ccData[0] == 0x20 && ccData[1] == 0x03` (Basic REPORT) and
read `ccData[2]` (current value), optionally `ccData[3]` (target) and
`ccData[4]` (duration) for v2+ frames.

## 11c. Driving a Multilevel Switch (CC 0x26)

`SetMultilevelSwitch` sends a Multilevel Switch SET (Command Class
`0x26`, command `0x01`) to an already-included dimmer, blinds
controller, or fan-speed regulator. `value` is the wire byte:

| `value`          | Meaning                                            |
|------------------|----------------------------------------------------|
| `0x00`           | off / level 0                                      |
| `0x01..0x63`     | level 1..99 (`0x63` is the spec maximum, not 100)  |
| `0xFF`           | "restore last level" — node decides which          |

`duration` is the transition byte:

| `duration`       | Meaning                                            |
|------------------|----------------------------------------------------|
| `0x00`           | instant — change immediately                       |
| `0x01..0x7F`     | seconds                                            |
| `0x80..0xFE`     | minutes (count = byte − `0x7F`, so `0x80` = 1 min) |
| `0xFF`           | factory-default duration for the device            |

The daemon always emits the v2 wire form (value + duration). v1-only
devices ignore the trailing duration byte per spec.

```bash
# Set node 5 to 50 % (decimal 50 → 0x32) instantly, callback id 7:
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 SetMultilevelSwitch yyyy 5 50 0 7

# Ramp node 5 to 99 % over 5 seconds, callback id 8:
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 SetMultilevelSwitch yyyy 5 99 5 8

# Restore node 5's last level with the device's default ramp:
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 SetMultilevelSwitch yyyy 5 0xFF 0xFF 9
```

Completion uses the same `SendDataStatus(callbackId, txStatus)` table
as `SetSwitchBinary`.

To read the current state, call `GetMultilevelSwitch`. The node's
reply arrives asynchronously as a typed `SwitchMultilevelReport(
sourceNodeId, currentValue, targetValue, duration)` signal alongside
the raw `ApplicationCommand`. For v1 wire-form reports `targetValue`
mirrors `currentValue` and `duration` is `0`; for v2+ reports the
three fields carry the live transition triple.

```bash
# Query node 5's current Multilevel Switch value, callback id 9:
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 GetMultilevelSwitch yy 5 9
```

The same typed signal also fires whenever the node sends an
unsolicited Report after a manual dim, programmed scene change, or
the tail end of an in-flight transition.

## 11d. Reading a multilevel sensor (CC 0x31)

`GetSensorMultilevel(nodeId, callbackId)` asks a node for its primary
sensor reading (temperature, humidity, luminance, …). The reply arrives
as the typed `SensorMultilevelReport(y y y y i)` signal:

| Field | Type | Meaning |
|-------|------|---------|
| `sourceNodeId` | `y` | the reporting node |
| `sensorType` | `y` | 0x01 air temp, 0x03 luminance, 0x05 humidity, … (SDS13781) |
| `scale` | `y` | unit selector, sensor-type-specific (air temp 0=°C, 1=°F) |
| `precision` | `y` | decimal-point shift — the reading is `value / 10^precision` |
| `value` | `i` | raw signed value (1/2/4-byte field, sign-extended) |

```bash
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 GetSensorMultilevel yy 5 7
# e.g. → SensorMultilevelReport y y y y i  5 1 0 1 215   = 21.5 °C
```

Only the v1 Get (primary sensor, no type filter) is implemented;
`SUPPORTED_GET`/`REPORT` and the v5+ type-filtered Get are not. In the
terminal, the `[g]` Get submenu → `[s]` issues the Get and reports render as
`Air temperature=21.5 C`.

## 11e. Reading notifications (CC 0x71)

The Notification Command Class carries push events from sensors —
motion, water leak, smoke, tamper, door/window, access control, … Most
nodes send these unsolicited (see §12), but `GetNotification(nodeId,
notificationType, callbackId)` polls a node for the latest pending event
of a given type. Both the polled reply and unsolicited reports arrive as
the typed `NotificationReport(y y y y ay)` signal:

| Field | Type | Meaning |
|-------|------|---------|
| `sourceNodeId` | `y` | the reporting node |
| `notificationType` | `y` | 0x05 water, 0x06 access control, 0x07 home security, … (SDS13713) |
| `event` | `y` | type-scoped event code (e.g. home-security 0x08 = motion detected) |
| `status` | `y` | notificationStatus — `0xFF` active, `0x00` cleared/idle |
| `parameters` | `ay` | optional event-parameter bytes, carried through verbatim |

```bash
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 GetNotification yyy 5 7 9
# e.g. → NotificationReport y y y y ay  5 7 8 255 0   = node 5 home-security motion
```

Only the v3 type-scoped Get and the v3+ Report are decoded; the v1 alarm
Get/Report, `Set`, and `SUPPORTED_GET`/`REPORT` are not. The
`(notificationType, event)` matrix is large, so the daemon forwards the
raw triple and leaves interpretation to the client. In the terminal,
the `[g]` Get submenu → `[n]` issues the Get and reports render as
`NotificationReport node=5 type=0x07 event=0x08 status=0xff`.

## 11f. Reading a binary sensor (CC 0x30)

The Sensor Binary Command Class carries simple open/closed / motion /
tamper state. It is officially deprecated by Notification (CC 0x71, §11e)
but is still present on most legacy contact and motion sensors — many
controllers map both to the same semantic event, so decode both and dedupe
upstream. `GetSensorBinary(nodeId, callbackId)` polls a node; the reply and
any unsolicited Reports arrive as the typed `SensorBinaryReport(y y y)`
signal:

| Field | Type | Meaning |
|-------|------|---------|
| `sourceNodeId` | `y` | the reporting node |
| `sensorType` | `y` | `0` for a v1 report (no type byte on the wire); device sensor-type code for v2+ |
| `value` | `y` | `0x00` idle / `0xFF` active (triggered) |

```bash
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 GetSensorBinary yy 5 7
# e.g. → SensorBinaryReport y y y  5 0 255   = node 5 active
```

Only the v1 Get (primary sensor, no type filter) is implemented; the v2
type-filtered Get and `SUPPORTED_GET`/`REPORT` are not. The decoder accepts
both v1 (single value byte) and v2 (value + sensorType) Reports. In the
terminal, the `[g]` Get submenu → `[i]` issues the Get and reports render as
`SensorBinaryReport node=5 active`.

## 11g. Reading a meter (CC 0x32)

The Meter Command Class reports accumulated consumption — electric
(kWh/W/V/A), gas, water — and is common on smart plugs.
`GetMeter(nodeId, scale, callbackId)` polls a node for a given scale; the
reply and any unsolicited Reports arrive as the typed
`MeterReport(y y y y y i q i b)` signal:

| Field | Type | Meaning |
|-------|------|---------|
| `sourceNodeId` | `y` | the reporting node |
| `meterType` | `y` | 1 electric, 2 gas, 3 water |
| `rateType` | `y` | 1 import, 2 export (v2+) |
| `scale` | `y` | unit selector, meterType-specific (electric 0=kWh, 2=W, …) |
| `precision` | `y` | decimal-point shift — the reading is `value / 10^precision` |
| `value` | `i` | raw signed current reading (1/2/4-byte field, sign-extended) |
| `deltaTime` | `q` | seconds since the previous sample; `0` = none |
| `previousValue` | `i` | raw signed previous reading (valid only when `hasPrevious`) |
| `hasPrevious` | `b` | true when `deltaTime != 0` and a previous value was present |

When `hasPrevious` is true the client can compute an instantaneous rate as
`(value - previousValue) / 10^precision / deltaTime` without keeping any
state of its own.

```bash
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 GetMeter yyy 5 0 7
# e.g. → MeterReport y y y y y i q i b  5 1 1 0 3 12345 3600 12000 true   = 12.345 kWh
```

Only the v3+ Report shape and a v2+ scale-selecting Get are implemented;
the v4 dual-scale extension, `SUPPORTED_GET`/`REPORT`, and `RESET` are not.
In the terminal, the `[g]` Get submenu → `[e]` prompts for node + scale,
issues the Get, and reports render as
`MeterReport node=5 electric 12.345 kWh (Δ3600s, prev 12.000)`.

## 11h. Thermostat mode (CC 0x40)

The Thermostat Mode Command Class sets/reads an HVAC unit's operating mode.
It is the first CC of the thermostat quartet (Setpoint 0x43, Operating
State 0x42, Fan Mode 0x44 follow). `SetThermostatMode(nodeId, mode,
callbackId)` changes the mode; `GetThermostatMode(nodeId, callbackId)`
queries it. The reply and any unsolicited Reports arrive as the typed
`ThermostatModeReport(y y)` signal:

| Field | Type | Meaning |
|-------|------|---------|
| `sourceNodeId` | `y` | the reporting node |
| `mode` | `y` | 0 off, 1 heat, 2 cool, 3 auto, 4 aux heat, 6 fan only, 8 dry, 10 auto changeover, 11 energy-save heat, 12 energy-save cool, 13 away |

```bash
# set node 5 to heat, then read it back
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 SetThermostatMode yyy 5 1 7
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 GetThermostatMode yy 5 8
# → ThermostatModeReport y y  5 1   = node 5 heat
```

The decoder masks the mode byte's low 5 bits (the high 3 bits are a v3
manufacturer-data-field count) and returns the raw mode; naming is left to
clients. `SUPPORTED_GET`/`REPORT` are not implemented. In the terminal,
the `[c]` Control submenu → `[t]` sets the mode and the `[g]` Get submenu →
`[t]` gets it; reports render as `ThermostatModeReport node=5 mode=heat`.

## 11i. Thermostat setpoint (CC 0x43)

The Thermostat Setpoint Command Class sets/reads a target temperature, one
per setpoint type (heating, cooling, …). The temperature uses the same
precision/scale/size encoding as Sensor Multilevel (§11d).
`SetThermostatSetpoint(nodeId, setpointType, precision, scale, value,
callbackId)` writes a target; `GetThermostatSetpoint(nodeId, setpointType,
callbackId)` reads one. The reply and any unsolicited Reports arrive as the
typed `ThermostatSetpointReport(y y y y i)` signal:

| Field | Type | Meaning |
|-------|------|---------|
| `sourceNodeId` | `y` | the reporting node |
| `setpointType` | `y` | 1 heating, 2 cooling, 7 furnace, 8 dry air, 9 moist air, 10 auto changeover |
| `scale` | `y` | 0 = °C, 1 = °F |
| `precision` | `y` | decimal-point shift — the target is `value / 10^precision` |
| `value` | `i` | raw signed value (1/2/4-byte field, sign-extended) |

The Set's `value` + `precision` + `scale` are encoded into the wire's
size/precision/scale flag byte for you — pick the precision you want (e.g.
`precision=1, value=215` for 21.5°) and the smallest byte width that fits
the value is chosen automatically.

```bash
# set node 5's heating setpoint to 21.5 °C (type 1, precision 1, scale 0, value 215)
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 SetThermostatSetpoint yyyyiy 5 1 1 0 215 7
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 GetThermostatSetpoint yyy 5 1 8
# → ThermostatSetpointReport y y y y i  5 1 0 1 215   = 21.5 °C heating
```

`SUPPORTED_GET`/`REPORT` and `CAPABILITIES_GET` are not implemented. In the
terminal, the `[c]` Control submenu → `[p]` sets a setpoint (prompts node,
type, precision, scale, value) and the `[g]` Get submenu → `[p]` reads one;
reports render as `ThermostatSetpointReport node=5 type=1 21.5 C`.

## 11j. Thermostat operating state (CC 0x42)

The Thermostat Operating State Command Class is a **read-only** report of
what the HVAC is currently doing. `GetThermostatOperatingState(nodeId,
callbackId)` polls it; the reply and any unsolicited Reports arrive as the
typed `ThermostatOperatingStateReport(y y)` signal:

| Field | Type | Meaning |
|-------|------|---------|
| `sourceNodeId` | `y` | the reporting node |
| `state` | `y` | 0 idle, 1 heating, 2 cooling, 3 fan only, 4 pending heat, 5 pending cool, 6 vent/economizer |

```bash
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 GetThermostatOperatingState yy 5 7
# → ThermostatOperatingStateReport y y  5 1   = node 5 heating
```

There is no Set (the state reflects the device, not a command). The decoder
masks the low 4 bits and returns the raw state; naming is left to clients.
`SUPPORTED_GET`/`REPORT` are not implemented. In the terminal, the `[g]` Get
submenu → `[o]` reads it; reports render as
`ThermostatOperatingStateReport node=5 state=heating`.

## 11k. Thermostat fan mode (CC 0x44)

The Thermostat Fan Mode Command Class sets/reads the HVAC fan behaviour.
`SetThermostatFanMode(nodeId, mode, off, callbackId)` sets it;
`GetThermostatFanMode(nodeId, callbackId)` reads it. The reply and any
unsolicited Reports arrive as the typed `ThermostatFanModeReport(y y b)`
signal:

| Field | Type | Meaning |
|-------|------|---------|
| `sourceNodeId` | `y` | the reporting node |
| `mode` | `y` | 0 auto low, 1 low, 2 auto high, 3 high, 4 auto medium, 5 medium |
| `off` | `b` | fan-off flag (bit 7 of the wire byte), carried separately from the mode |

The wire packs `mode` (low 4 bits) and the `off` flag (bit 7) into one
byte; the daemon splits them on decode and re-packs them on Set, so callers
pass the two fields independently.

```bash
# set node 5's fan to "auto high" (not off), then read it back
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 SetThermostatFanMode yyby 5 2 false 7
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 GetThermostatFanMode yy 5 8
# → ThermostatFanModeReport y y b  5 2 false   = node 5 auto high
```

`SUPPORTED_GET`/`REPORT` are not implemented. In the terminal, the `[c]`
Control submenu → `[n]` sets the fan mode and the `[g]` Get submenu → `[f]`
reads it; reports render as `ThermostatFanModeReport node=5 mode=auto high`.

This completes the thermostat HVAC quartet (Mode 0x40, Setpoint 0x43,
Operating State 0x42, Fan Mode 0x44).

## 11l. Color switch (CC 0x33)

The Color Switch Command Class drives multi-component RGB / RGBW / CCT
lighting — each colour component (red, green, blue, white, …) has its own
0–255 level. `SetColorSwitch(nodeId, components, duration, callbackId)`
writes one or more components atomically; `GetColorSwitch(nodeId,
componentId, callbackId)` reads one. The reply and any unsolicited Reports
arrive as the typed `ColorSwitchReport(y y y y y)` signal:

| Field | Type | Meaning |
|-------|------|---------|
| `sourceNodeId` | `y` | the reporting node |
| `componentId` | `y` | 0 warm white, 1 cold white, 2 red, 3 green, 4 blue, 5 amber, 6 cyan, 7 purple |
| `value` | `y` | current level 0–255 |
| `targetValue` | `y` | transition target (mirrors `value` on v1 reports) |
| `duration` | `y` | 0 instant, 1–0x7F sec, 0x80–0xFE min, 0xFF default |

`components` on Set is an array of bytes holding **alternating
`(componentId, value)` pairs** — e.g. `{2,255, 3,0, 4,0}` sets red full,
green and blue off in a single frame.

```bash
# set node 5 to full red, then read the red component back
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 SetColorSwitch yayyy 5 6 2 255 3 0 4 0 255 7
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 GetColorSwitch yyy 5 2 8
# → ColorSwitchReport y y y y y  5 2 255 255 0   = node 5 red=255
```

Only Set / Get / Report are implemented; `SUPPORTED_GET`/`REPORT` and
`START`/`STOP_LEVEL_CHANGE` (fade effects) are deferred. In the terminal,
the `[c]` Control submenu → `[l]` prompts for an RGB triple and sets it, and
the `[g]` Get submenu → `[k]` reads one component; reports render as
`ColorSwitchReport node=5 red=255`.

## 11m. Central scene (CC 0x5B)

The Central Scene Command Class is **push-only**: a remote or wall switch
tells the controller "button N was pressed" (single / double / triple tap,
hold, release). The daemon never sends Central Scene frames — there is no
Get or Set — it only decodes inbound NOTIFICATION frames and re-emits them
as the typed `CentralSceneNotification(y y y y b)` signal:

| Field | Type | Meaning |
|-------|------|---------|
| `sourceNodeId` | `y` | the node whose button was pressed |
| `sequenceNumber` | `y` | rolling counter — de-dup repeats with the same value |
| `keyAttribute` | `y` | 0 press 1×, 1 release, 2 hold, 3 press 2×, 4 press 3×, 5 press 4×, 6 press 5× |
| `sceneNumber` | `y` | which button / scene |
| `slowRefresh` | `b` | v2+ slow-refresh flag for held keys |

```bash
# watch for button presses (no method call — the node pushes these)
busctl --system monitor com.tiunda.ZWaved
# → CentralSceneNotification y y y y b  5 10 0 1 false   = node 5, scene 1, single press
```

Only the v1+ NOTIFICATION is decoded; `SUPPORTED_GET`/`REPORT` and the v3+
Configuration triplet are deferred. The terminal renders these in the
activity pane (`CentralSceneNotification node=5 scene=1 press 1x seq=10`);
there is no key binding because the daemon can't initiate them.

## 11n. Door lock + user code (CC 0x62 / 0x63)

Locks pair two CCs: **Door Lock** (lock/unlock + state) and **User Code**
(per-slot PINs).

> ⚠️ Almost every real lock requires **Security S0 or S2** transport
> (CC 0x98 / 0x9F, not yet implemented). These methods exercise the
> *unencrypted* wire shape — useful for tests and S2-capable test rigs, but
> a production lock will ignore unencrypted Door Lock / User Code frames
> until the security epics land.

### Door Lock (0x62)

`SetDoorLock(nodeId, mode, callbackId)` sets the lock; `GetDoorLock(nodeId,
callbackId)` reads it. Reports arrive as `DoorLockOperationReport(y y y y y y
y y)` = sourceNodeId, currentMode, handlesMode, condition,
lockTimeoutMinutes, lockTimeoutSeconds, targetMode (v4+), duration (v4+).
`mode`/`currentMode`: 0x00 unsecured, 0x01 unsecured w/ timeout, 0x10
inside-handles, 0x11 inside-handles w/ timeout, 0xFF secured.

```bash
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 SetDoorLock yyy 5 255 7   # 0xFF = secured
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 GetDoorLock yy 5 8
```

### User Code (0x63)

`SetUserCode(nodeId, userIdentifier, userIdStatus, userCode, callbackId)`
writes a slot (`userCode` is the raw 4–10 ASCII digits as `ay`; status 0x00
available/clear, 0x01 enabled). `GetUserCode(nodeId, userIdentifier,
callbackId)` reads one slot → `UserCodeReport(y y y ay)`.
`GetUserCodeCount(nodeId, callbackId)` → `UserCodeUsersNumberReport(y y)`
(how many slots the lock has).

```bash
# set slot 2 to PIN "1234" (0x31 0x32 0x33 0x34), enabled
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 SetUserCode yyyayy 5 2 1 4 0x31 0x32 0x33 0x34 7
```

Door Lock CONFIGURATION (auto-relock) and User Code v2 extended fields are
deferred. In the terminal, the `[c]` Control submenu has `[d]` Door lock set
and `[u]` User code set; the `[g]` Get submenu has `[d]` Door lock, `[u]`
User code, and `[x]` User code slot count.

## 11o. Driving an Indicator (CC 0x87)

Controls a node's indicator (LED / buzzer). v1 is a single value byte:
`0x00` off, `0x01..0x63` level, `0xFF` on — same shape as Basic. Useful
for "find this node" blink or status LEDs.

| Method | Signature | Notes |
|--------|-----------|-------|
| `SetIndicator` | `(y y y) → ()` | nodeId, value, callbackId |
| `GetIndicator` | `(y y) → ()` | nodeId, callbackId; reply arrives as the `IndicatorReport` signal |

```bash
# Turn node 5's indicator on, callback id 7:
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 SetIndicator yyy 5 0xFF 7
# Read it back:
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 GetIndicator yy 5 8
# → later: IndicatorReport y y  5 255
```

Unsolicited / GET replies are decoded by the cc-translator into the typed
`IndicatorReport(y y)` signal — `(sourceNodeId, value)`. The terminal's
`[c]` Control submenu → `[i]` sets an indicator value, and the report
renders in the activity pane. The v3+ structured (multi-indicator) form is
a follow-up; v1 covers the common single-indicator case.

## 11p. Supervised send (CC 0x6C)

Supervision wraps an outbound CC frame so the node returns an explicit
**applied/working/fail** status — distinguishing "the bytes were
transmitted" (the `SendDataStatus` answer) from "the node actually obeyed".

`SendSupervised(nodeId, sessionId, ccData, callbackId)` encapsulates the raw
`ccData` (a complete inner CC frame) in a Supervision GET. `sessionId` is a
caller-chosen 6-bit nonce (0–63) that the node echoes in its report, so the
caller correlates the reply to the request.

```bash
# Supervised "Binary Switch SET on" (inner = 0x25 0x01 0xFF) to node 5,
# session nonce 7, callback 9:
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 SendSupervised yyayy 5 7 3 0x25 0x01 0xFF 9
# → later: SupervisionReport y y b y y  5 7 false 255 0   (session 7, status success)
```

The reply arrives as the typed `SupervisionReport(y y b y y)` signal —
`(sourceNodeId, sessionId, moreStatusUpdates, status, duration)`. `status`:
`0x00` no-support, `0x01` working, `0x02` fail, `0xFF` success;
`moreStatusUpdates` means a final report follows after `duration`. The
terminal renders it in the activity pane (`status=success`).

**Scope:** this is the MVP — a generic supervised send keyed on a
caller-chosen session. A daemon-side session table and a `supervised` toggle
on every per-CC Set (so e.g. `SetSwitchBinary` returns the applied status)
are a follow-up, best built alongside the closed-loop automation epic (#101,
verify-after-set).

## 11q. Multi Channel — addressing endpoints (CC 0x60)

A multi-endpoint node (e.g. a 2-gang switch, each gang an endpoint) is driven
by **encapsulating** the inner CC frame for a specific endpoint. The daemon
can already configure `(node, endpoint)` members in a Multi Channel
Association group (§14b); these methods let it actually *address* those
endpoints and *discover* them.

| Method | Signature | Notes |
|--------|-----------|-------|
| `SendDataToEndpoint` | `(y y ay y) → ()` | nodeId, endpoint, inner ccData, callbackId — wraps ccData in `MULTI_CHANNEL_CMD_ENCAP` to the endpoint |
| `GetMultiChannelEndpoints` | `(y y) → ()` | nodeId, callbackId; reply → `MultiChannelEndPointReport` signal |
| `GetMultiChannelCapability` | `(y y y) → ()` | nodeId, endpoint, callbackId; reply → `MultiChannelCapabilityReport` signal |

```bash
# Turn endpoint 2 of node 5 on — inner = Binary Switch SET on (0x25 0x01 0xFF):
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 SendDataToEndpoint yyayy 5 2 3 0x25 0x01 0xFF 7
# Discover endpoints:
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 GetMultiChannelEndpoints yy 5 8
# → later: MultiChannelEndPointReport y y b b  5 2 false true   (2 endpoints, identical)
# Query endpoint 2's capabilities:
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 GetMultiChannelCapability yyy 5 2 9
# → later: MultiChannelCapabilityReport y y y y ay  5 2 16 1 [0x25]
```

Discovery replies are decoded into the typed `MultiChannelEndPointReport(y y b
b)` and `MultiChannelCapabilityReport(y y y y ay)` signals. The terminal's
`[g]` Get submenu has `[h]` Multi Channel endpoints and `[j]` Multi Channel
capability.

**Scope:** this is the controller side — discover endpoints and address an
endpoint on send. Persisting the discovered endpoint table in NodeRegistry,
and *unwrapping inbound encapsulated replies* into endpoint-tagged typed
reports (so a Report from endpoint 2 surfaces as such), are follow-ups — the
latter is #146 (shared with the E1 Tier 2 responder work). For now an
encapsulated inbound reply arrives as a raw `ApplicationCommand` with
`ccData[0] == 0x60 && ccData[1] == 0x0D`.

## 12. Unsolicited node events

When a node sends an unsolicited Command Class frame — most commonly a
Binary Switch `REPORT` after the user manually flips a wall switch, but
also sensor pings and other notifications — the dongle delivers it via
`FUNC_ID_APPLICATION_COMMAND_HANDLER` (0x04). `zwaved` decodes the
frame and re-broadcasts it on D-Bus in two parallel forms:

- **`ApplicationCommand(y y ay)`** — `(rxStatus, sourceNodeId, ccBytes)`.
  The raw passthrough so any client can decode CCs that `zwaved` itself
  doesn't know about.
- **`SwitchBinaryReport(y y)`** — `(sourceNodeId, state)` where `state`
  is `0` Off, `1` On, `2` Unknown. Emitted only when the CC bytes parse
  as a Binary Switch Report (CC `0x25`, command `0x03`).
- **`SwitchMultilevelReport(y y y y)`** — `(sourceNodeId, currentValue,
  targetValue, duration)`. Emitted only when the CC bytes parse as a
  Multilevel Switch Report (CC `0x26`, command `0x03`). For v1 wire
  form the decoder mirrors `currentValue` into `targetValue` and
  leaves `duration` at `0`.

Both typed signals are visible in `busctl --system monitor com.tiunda.ZWaved`
alongside the raw `ApplicationCommand`. Use the typed signal when you
only care about that specific CC; use the raw signal when you need to
handle arbitrary CCs.

**Transport-layer unwrapping.** Some inbound frames arrive wrapped in a
transport/integrity CC. The daemon verifies/unwraps these transparently and
re-broadcasts the *inner* CC as a fresh `ApplicationCommand`, so both the raw
and typed forms above work unchanged — no client-side handling needed:

- **CRC-16 Encapsulation (CC `0x56`, #28)** — a pre-S0 integrity wrapper some
  legacy non-secure devices still use. The daemon checks the CRC-16/AUG-CCITT
  trailer and, on success, republishes the inner frame; a frame that fails the
  CRC is dropped (logged as a warning), never surfaced.

- **Transport Service (CC `0x55`, #25)** — segmentation for datagrams longer
  than the radio MTU. A long inner CC frame is split by the sender into a
  `FIRST_SEGMENT` plus one or more `SUBSEQUENT_SEGMENT`s, each carrying the
  total datagram size, a session id, its byte offset and a 2-byte CRC-16 Frame
  Check Sequence. The daemon reassembles per source node — segments may arrive
  out of order, each is placed at its offset, and the inner frame is
  republished only once every byte is present; a segment that fails its FCS is
  dropped. Reassembly only (the MVP): the daemon never *sends* segmented
  datagrams yet, and `SEGMENT_REQUEST` retransmit, inactivity timeout and
  concurrent multi-session reassembly are deferred follow-ups.

- **Security S0 (CC `0x98`, #26)** — encrypted command encapsulation. When a
  node sends a `MESSAGE_ENCAPSULATION` frame, the daemon recovers the nonce it
  had issued that node, authenticates the frame (AES-128 CBC-MAC) and decrypts
  it (AES-128-OFB) with the network key, then republishes the inner CC frame so
  the normal decoders run. A frame that fails authentication, or references a
  nonce the daemon never issued / that has expired, is dropped (logged as a
  warning), never surfaced. On the first successfully-decrypted frame from a
  node the daemon emits a **`NodeSecurityStatus(nodeId, secure)`** signal
  (retained — a late subscriber learns which nodes are secure). The network key
  lives at the `[security] s0_key_file` path (default
  `<state_dir>/security/s0.key`, mode `0600`); **back it up — losing it forces
  re-inclusion of every secure node.** When a newly-included node advertises
  CC `0x98`, the daemon runs the S0 inclusion bootstrap (scheme negotiation +
  network-key exchange) and marks the node secure on success. From then on the
  daemon transparently encapsulates its *outbound* commands to that node
  (fetching a fresh nonce per message). The full S0 wire path is pending
  end-to-end verification on real hardware (#168).

- **Security S2 (CC `0x9F`, #27)** — second-generation encrypted transport
  (AES-128-CCM + Curve25519 ECDH inclusion + per-class network keys). Built in
  layers; the inclusion **key-agreement** phase is wired today. When a
  newly-included node advertises CC `0x9F`, the daemon runs the KEX handshake
  (`KEX_GET → KEX_REPORT → KEX_SET`), exchanges public keys, and — once it has
  the node's full public key — derives the temporary bootstrap-channel keys via
  ECDH. For the **Authenticated / Access Control** classes the joining node
  obfuscates the first group of its DSK (Device Specific Key), so the daemon
  raises a **`DSKPendingConfirmation(nodeId, dsk)`** signal (retained, so a
  client connecting after the prompt still sees it). `dsk` is the *partial* DSK
  with the first group shown as `00000`; read the full DSK off the device label
  and supply the missing 5-digit PIN:

  ```bash
  busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
      com.tiunda.ZWaved1 ConfirmDSK ys 12 "54321"
  ```

  The daemon restores the obfuscated key bytes from the PIN and resumes key
  agreement (an `S2 Unauthenticated` node needs no PIN and proceeds straight
  through). A malformed PIN is ignored and the prompt stays up; a
  well-formed-but-wrong PIN only fails later, when the encrypted handshake can't
  authenticate. The remaining S2 phases — the encrypted temp-channel key
  install, live transport encapsulation, and on-bench acceptance — are in
  progress (#187 / #199 / #189); the S2 wire path is unverified against real
  hardware until #189.

## 13. Listing nodes

`GetNodes` returns the daemon's in-memory list of currently-included
nodes. Each entry carries the device-class triple
(`basic` / `generic` / `specific`) and the supported command-class list
captured at inclusion time.

```bash
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 GetNodes
```

Sample output for one node:

```
a(yyyyay) 1 5 4 16 1 5 0x25 0x70 0x86 0x59 0x85
```

The registry is backed by SQLite (`${ZWAVED_STATE_DIR:-/var/lib/zwaved}/nodes.db`)
so the list survives both USB reconnects and daemon restarts. The
schema captures only the static info from inclusion (device-class
triple + command classes); dynamic per-node state continues to flow
through the CC-specific signals (`SwitchBinaryReport`, etc.) rather
than being duplicated in the database. Rows are keyed by
`(home_id, node_id)` — swapping the dongle for one belonging to a
different Z-Wave network loads only that network's nodes; entries
for the previous network stay in the database, just out of view. If
the state directory can't be created or opened, the daemon logs a
warning and falls back to in-memory only.

## 14. Managing associations (CC 0x85)

Association groups let a node push unsolicited commands to other nodes
without controller mediation. Group 1 is conventionally the lifeline
group, owned by the primary controller — that's why a wall switch's
manual toggle reaches the daemon as a `Basic Set` (see §12). The four
methods cover query and configuration:

```bash
# How many groups does node 12 expose?
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 GetAssociationGroupings yy 12 1
# → AssociationGroupingsReport y y 12 5

# Who's currently in node 12's group 1?
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 GetAssociation yyy 12 1 2
# → AssociationReport y y y y ay 12 1 5 0 1   (group 1, max 5, 0 to follow, member: node 1)

# Add nodes 3 and 7 to node 12's group 2:
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 SetAssociation yyayy 12 2 2 3 7 3

# Remove all members from node 12's group 2:
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 RemoveAssociation yyayy 12 2 0 4
```

`AssociationReport` carries `(sourceNodeId, groupId, maxSupported,
reportsToFollow, members)`. When a group's member list spans multiple
frames, `reportsToFollow` is non-zero on all but the last; clients
should accumulate `members` across reports until they see zero.

`AssociationGroupingsReport` carries `(sourceNodeId, supportedGroupings)`.

`SendDataStatus` arrives separately for each Set/Remove/Get/Groupings
call (echoing the `callbackId` you passed); a successful Get/Groupings
will be followed shortly after by the matching Report signal.

### Auto-lifeline on inclusion

Z-Wave Plus nodes ship with their lifeline (group 1) empty and expect
the including controller to populate it. The daemon does this
automatically: when an inclusion completes and the node's *supported*
CC list (the bytes before `0xEF` `COMMAND_CLASS_MARK`) contains both
`0x5E` (`COMMAND_CLASS_ZWAVEPLUS_INFO`) and `0x85`
(`COMMAND_CLASS_ASSOCIATION`), zwaved queues a
`SetAssociation(nodeId, group=1, members=[controllerNodeId])` of its
own at the end of the inclusion sequence. No client action is needed.

The auto-lifeline `SetAssociation` is sent with `callbackId=0`, so it
does not produce a `SendDataStatus` signal; the operator-visible
artefact is the daemon log line
`[ProtocolThread] auto-lifeline: SetAssociation node=N group=1 controller=C`.
Re-running the auto-lifeline on an already-populated group is a no-op
on the wire (Association SET is idempotent for already-listed members),
so re-inclusion of the same device is safe. Non-Z-Wave-Plus nodes (no
0x5E in the supported list) are left untouched — set them up with the
explicit `[L]` terminal action or `SetAssociation` D-Bus method.

## 14b. Multi Channel Association (CC 0x8E)

For multi-endpoint devices (a Z-Wave thermostat with separate humidity
and temperature endpoints, a metering strip with a per-outlet endpoint,
…) plain Association can only target whole nodes — there's no way to
say *send the report to endpoint 3 of node 7*. Command Class `0x8E`
("Multi Channel Association") solves this by carrying both **node**
members (just like CC `0x85`) and **endpoint** members (`nodeId,
endpoint` pairs). The wire frame puts node members first, then a
`MARKER = 0x00` byte, then the pairs.

The four daemon methods mirror their plain-Association counterparts but
add a fifth argument carrying the endpoint pairs as `a(yy)`:

```bash
# How many MCA groups does node 12 expose?
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 GetMultichannelAssociationGroupings yy 12 1

# Read group 3 of node 12
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 GetMultichannelAssociation yyy 12 3 2

# Add a node member (node 5) and an endpoint member (node 7 endpoint 2)
# to node 12's group 3:
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 SetMultichannelAssociation 'yyaya(yy)y' \
    12 3 \
    1 5 \
    1 7 2 \
    9

# Remove that endpoint member, leaving the node member in place:
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 RemoveMultichannelAssociation 'yyaya(yy)y' \
    12 3 \
    0 \
    1 7 2 \
    10

# Remove ALL members (both kinds) — both arrays empty:
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 RemoveMultichannelAssociation 'yyaya(yy)y' \
    12 3 \
    0 \
    0 \
    11
```

Get/Groupings replies come back as **raw `ApplicationCommand` signals**
(rather than typed `MultichannelAssociationReport`), since clients that
care about MCA already need a CC decoder for the encompassing endpoint
encapsulation. Filter on `ccData[0] == 0x8E && ccData[1] == 0x03` for
REPORT and `ccData[1] == 0x06` for GROUPINGS REPORT; the wire shape is
documented in the Z-Wave Application Command Class spec §4.51.

`SendDataStatus` arrives separately for each Set/Remove/Get/Groupings
call (echoing the `callbackId` you passed).

## 15. Existing-network discovery (`InitData`)

In addition to `GET_VERSION` + `MEMORY_GET_ID`, the daemon now also runs
`FUNC_ID_SERIAL_API_GET_INIT_DATA` (`0x02`) when the serial port opens.
The dongle returns a node bitmap covering every node currently
included in the network — including the ones the daemon hasn't met
during this run. Each ID is seeded into the `NodeRegistry` (only if
not already present, so it never downgrades a fully-populated node),
which means **`GetNodes` reflects the full network on the first start
after a reinstall** instead of starting empty.

The full payload is also exposed:

```bash
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 GetInitData
# → (yyayy)  1 8 1 5 11 12 …  5 0
```

Fields: `serialApiVersion`, `capabilities` (bit 0=secondary,
1=no-send, 2=SIS, 3=real-primary), `nodeIds` (array of included node
IDs), `chipType` (1=400-series, 2=500, 5=700), `chipVersion` (silicon
revision).

The same payload is emitted as the `InitData(y y ay y y)` signal once
per serial-port open. Late D-Bus subscribers will miss the signal but
can still recover the latest snapshot via `GetInitData`.

Seeded nodes show in `GetNodes` with `basic`/`generic`/`specific`
zeroed and an empty CC list — we know they exist but not what they
are. Re-including them, or implementing `FUNC_ID_GET_NODE_PROTOCOL_INFO`
(`0x41`) as a follow-up, would fill in the device class and CC list.

## 16. Dongle introspection

When the protocol thread opens the serial port to a dongle, it sends
two host-API requests synchronously and caches the answers:

- `FUNC_ID_GET_VERSION` (`0x15`) → printable Z-Wave library version
  string + library type byte (`1` = Static Controller, `7` = Bridge,
  etc.)
- `FUNC_ID_MEMORY_GET_ID` (`0x20`) → 4-byte network Home ID +
  this controller's own 1-byte Node ID.

Both values are emitted on D-Bus as a `DongleInfo(s y ay y)` signal
and cached so `GetDongleInfo()` can return the latest snapshot to
clients that connect later. The daemon also logs the library type by
name on connect (e.g. `lib type 1 Static Controller`), and the
`zwave-terminal` `[i]` view renders it as `libType=1 (Static
Controller)`.

```bash
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 GetDongleInfo
```

To read the library type **without** the daemon running (e.g. to check
whether a dongle is a Bridge Controller before planning virtual-node
work), use the standalone probe:

```bash
scripts/zw-dongle-probe                 # defaults to /dev/ttyACM0
scripts/zw-dongle-probe --port /dev/ttyACM1
```

Library type `7` (Bridge Controller) is the only one that can host
virtual slave nodes; a Static Controller (`1`) cannot.

If a dongle has not yet been introspected (none plugged in since the
daemon started), `GetDongleInfo` returns an empty struct (empty
`libraryVersion`, all bytes zero); clients should treat that as
"not available yet" and wait for the next `DongleInfo` signal.

## 16b. Post-inclusion policies (Configuration / Association / Wake-Up)

The daemon keeps a **policy register**: for a device type (or a specific
node) it remembers which Configuration parameters, Associations, and
Wake-Up interval should be applied after inclusion. `InclusionOrchestrator`
applies the *effective* policy (device default merged with the per-node
override, override winning per entry) when a node finishes inclusion;
`WakeUpOrchestrator` can re-apply on wake-up. The lifeline (Association
group 1 → controller) is set automatically for nodes that support
Association when `[behavior] auto_lifeline` is on — it is **not** part of
the policy.

### Policy BLOB format

A policy crosses the wire as a single `ay` byte array — a versioned,
length-prefixed binary form:

```
u8  version = 1
u8  entryCount
entryCount × entry:
  u8 kind
  kind 1 (Configuration): u8 parameter, u8 size (1|2|4), u8 signed, i32 value (big-endian)
  kind 2 (Association):    u8 groupId, u8 memberCount, memberCount × u8 nodeId
  kind 3 (Wake-Up):        u32 intervalSeconds (big-endian), u8 notificationNodeId
```

An empty policy is `01 00`. A Wake-Up `notificationNodeId` of `0` means
"report to the daemon's controller node."

### Methods

| Method | Signature | Notes |
|--------|-----------|-------|
| `SetDevicePolicy` | `(q q q ay) → ()` | upsert; args are manufacturerId, productTypeId, productId, policy |
| `GetDevicePolicy` | `(q q q) → (ay)` | empty bytes if none |
| `DeleteDevicePolicy` | `(q q q) → ()` | |
| `ListDevicePolicies` | `() → (a(qqqay))` | rows of (mfr, type, id, policy) |
| `SetNodeOverride` | `(y ay) → ()` | scoped to the current network's home id |
| `GetNodeOverride` | `(y) → (ay)` | empty bytes if none |
| `DeleteNodeOverride` | `(y) → ()` | |
| `GetEffectivePolicy` | `(y) → (ay)` | merged device-default + override the orchestrators would apply now |

Each `Set` / `Delete` re-triggers the orchestrators on the next applicable
event. Example — set node 9's override to "Configuration parameter 3 = 1
(1 byte)". The 10-byte BLOB is `01 01 01 03 01 00 00 00 00 01` (version,
count=1, kind=config, param=3, size=1, signed=0, value=1); in `busctl`
the `ay` is written as a length followed by the bytes:

```bash
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 SetNodeOverride yay 9 10 1 1 1 3 1 0 0 0 0 1
```

### In the terminal

`zwave-terminal` drives this interactively under the `[p]` **Policy**
submenu: `[e]` view a node's effective policy, `[o]` view its override,
`[s]` add/update an override entry — Configuration, Association, or Wake-Up
— (preserves other entries), `[d]` delete the override, `[l]` list device
policies, and `[a]` author device policies (set an entry or delete a whole
policy, by manufacturer/type/product id).

## 16c. Node metadata (human labels)

Per-node, human-authored **descriptive** key/value strings — `name`,
`room`, `house`, `purpose`, `notes`, or any key you like. The daemon never
acts on this; it's so operators and UIs can label a network in human terms
("Kitchen light") instead of bare node IDs. Stored in `nodes.db`, scoped
per network. Distinct from policies (§16b), which are *behavioural*.

| Method | Signature | Notes |
|--------|-----------|-------|
| `SetNodeMetadata` | `(y s s) → ()` | nodeId, key, value; an **empty value clears the key** |
| `GetNodeMetadata` | `(y) → (a(ss))` | all key/value pairs for a node, ordered by key |
| `DeleteNodeMetadata` | `(y s) → ()` | clear one key |
| `GetNodesByMetadata` | `(s s) → (ay)` | **reverse lookup**: node ids carrying the exact tag `key=value`, ascending |

Every Set/Delete emits a `NodeMetadataChanged(y)` signal (retained) so a UI
can refresh without polling. The reverse lookup (`GetNodesByMetadata`) answers
"which nodes are tagged `room=living-room`" — the membership resolver the
logical thermostat (epic #131) builds on. Example — label node 5 and group by
room:

```bash
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 SetNodeMetadata yss 5 name "Kitchen light"
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 GetNodeMetadata y 5
# → a(ss)  1  "name" "Kitchen light"
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 GetNodesByMetadata ss room "Living room"
# → ay  2  7 12     (nodes 7 and 12 are tagged room="Living room")
```

## 16d. Scene control (daemon-side scenes)

The daemon runs **scenes** in response to physical button presses. A *scene*
is an ordered list of **actions** — each a raw Command Class frame (the same
bytes a `SendData` carries) addressed to a target node. A *trigger* binds a
physical press to a scene id, so the same selector from different controllers
can run different scenes ("scene 1" from the living room vs. the hallway). When
a bound press arrives, the SceneOrchestrator replays the scene's actions and
emits a `SceneActivated` signal. Stored in `nodes.db`, scoped per network.

A scene crosses the wire as `a(yay)` — a list of `(targetNodeId, ccPayload)`
structs; a trigger list as `a(yyyys)`.

**Trigger sources.** A trigger's leading `source` byte selects which Command
Class the press arrives on (#124). The `sceneNumber` field is reinterpreted as
a per-source *selector*, and `keyAttribute` is only meaningful for Central
Scene:

| `source` | CC | selector (`sceneNumber`) | `keyAttribute` |
|----------|----|--------------------------|----------------|
| `0` | Central Scene (0x5B) | scene number | press 1×/2×/hold/… |
| `1` | Basic Set (0x20) | Basic value (0/0xFF/level) | 0 (ignored) |
| `2` | Scene Activation (0x2B) | activation scene id | 0 (ignored) |

The `source` is part of the trigger key, so the same `(node, selector)` can be
bound independently on each source without colliding.

| Method | Signature | Notes |
|--------|-----------|-------|
| `SetScene` | `(s a(yay)) → ()` | upsert scene `sceneId` with ordered actions |
| `GetScene` | `(s) → (a(yay))` | the scene's actions; empty if no such scene |
| `DeleteScene` | `(s) → ()` | remove a scene (dangling triggers become no-ops) |
| `ListScenes` | `() → (as)` | all scene ids for the network, ascending |
| `BindSceneTrigger` | `(y y y y s) → ()` | source, sourceNodeId, selector, keyAttribute → sceneId |
| `UnbindSceneTrigger` | `(y y y y) → ()` | remove a press binding |
| `ListSceneTriggers` | `() → (a(yyyys))` | rows of (source, sourceNodeId, selector, keyAttribute, sceneId) |

When a scene runs, the daemon emits `SceneActivated(y y y s u)` —
`(sourceNodeId, sceneNumber, keyAttribute, sceneId, actionCount)` (for Basic /
Scene Activation triggers `sceneNumber` carries the selector and `keyAttribute`
is 0). A press that resolves to a deleted scene still fires `SceneActivated`
with `actionCount = 0`; an unbound press fires nothing. The two non-Central
sources also surface as their own observability signals — `BasicSetReceived(y
y)` and `SceneActivationSet(y y y)` — fired whenever such a frame arrives,
whether or not it's bound to a scene.

Example — make a scene "tv" that turns node 5 on (`0x25 0x01 0xFF`) and node 6
off (`0x25 0x01 0x00`), then run it from press 1× of scene 1 on Central Scene
controller 7:

```bash
# Define the scene: a(yay) = 2 actions
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 SetScene "sa(yay)" tv 2 \
    5 3 0x25 0x01 0xFF \
    6 3 0x25 0x01 0x00
# Bind source 0 (Central Scene), controller 7, scene 1, press 1x (key 0)
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 BindSceneTrigger yyyys 0 7 1 0 tv
# Bind the same scene to a Basic Set "on" (0xFF=255) from node 8 (source 1)
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 BindSceneTrigger yyyys 1 8 255 0 tv
# Inspect
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 ListSceneTriggers
# → a(yyyys)  2  0 7 1 0 "tv"  1 8 255 0 "tv"
```

(In the `SetScene` call each action is `targetNodeId`, then the `ay` payload as
a length followed by its bytes — `5 3 0x25 0x01 0xFF` is "node 5, 3-byte
payload `25 01 FF`".)

`zwave-terminal` drives all of this from the `[e]` **Scenes** submenu: `[l]`
list scenes (with their actions), `[t]` list triggers (tagged by source), `[s]`
set/edit a scene (prompts a scene id, then repeatedly asks for a target node +
CC payload bytes until you leave the node blank), `[d]` delete a scene, `[b]`
bind a trigger (prompts the source kind first, then the source-appropriate
selector), `[u]` unbind one. When a scene runs, the `SceneActivated` signal
lands in the activity pane — e.g. `SceneActivated node=7 scene=1 1x -> "tv" (2
actions)`; inbound `BasicSetReceived` / `SceneActivationSet` frames are logged
there too.

## 16e. Logical thermostats (climate-group control)

A **logical thermostat** is the set of climate nodes that share a node-metadata
tag (§16c) — e.g. every node with `room = Living room`. Writing a mode or
setpoint to the logical thermostat fans it out to all tagged members that
support the relevant Thermostat CC; member reports are aggregated back into one
logical state. Pure orchestration (no virtual node); membership is *derived*
from metadata at call time, so re-tagging a node moves it between thermostats
with no extra bookkeeping. Aggregation only — closed-loop control (setpoint vs
measured temperature, schedules) is a separate future epic.

| Method | Signature | Notes |
|--------|-----------|-------|
| `SetLogicalThermostatMode` | `(s s y) → ()` | groupKey, groupValue, mode → fans out a Thermostat Mode Set to each member supporting CC 0x40 |
| `SetLogicalThermostatSetpoint` | `(s s y y y i) → ()` | groupKey, groupValue, setpointType, precision, scale, value → fans out to members supporting CC 0x43 |
| `GetLogicalThermostatState` | `(s s) → (y y y y y y y i)` | aggregated (memberCount, mode, operatingState, fanMode, setpointType, setpointScale, setpointPrecision, setpointValue) |

Aggregation rules: `mode` / `fanMode` report the common member value, or `0xFF`
(**mixed**) when members disagree; `operatingState` is "active"
(heating/cooling) if any member is; the setpoint fields carry the
most-recently-reported member setpoint. `memberCount` distinguishes "no members
tagged" from "idle". A `GetLogicalThermostatState` for a group that hasn't been
addressed *and* had a member report yet returns all-zeros.

Whenever the aggregate changes, the daemon emits
`LogicalThermostatStateChanged(s s y y y y y y y i)` — the group identity
followed by the same aggregated fields — so a UI can track every logical
thermostat without polling.

Example — drive all `room = Living room` thermostats to heat (mode 1) and 21.5 °C
(setpoint type 1 heating, precision 1, scale 0 = °C, raw value 215):

```bash
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 SetLogicalThermostatMode ssy room "Living room" 1
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 SetLogicalThermostatSetpoint ssyyyi room "Living room" 1 1 0 215
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 GetLogicalThermostatState ss room "Living room"
# → y y y y y y y i   2 1 1 0 1 0 1 215   (2 members, mode heat, heating, …, 21.5°C)
```

(Tag members first with `SetNodeMetadata` / group with `GetNodesByMetadata`, §16c.)

`zwave-terminal` drives this from the `[h]` **Logical thermostat** submenu: `[m]`
set group mode, `[s]` set group setpoint, `[g]` read the aggregated group state
(all prompt the group key + value first). `LogicalThermostatStateChanged` lands
in the activity pane as `LogicalThermostat room=Living room members=2 mode=heat
op=1 fan=… setpoint=215` (mode/fan render as `mixed` when members disagree).

## 17. Future: ubus

A second backend implementing the same methods/signals over OpenWrt's
ubus is on the roadmap. The CMake cache option `ZWAVED_EXTERNAL_API`
already accepts `ubus` and `both`, but only `dbus` is implemented today.
