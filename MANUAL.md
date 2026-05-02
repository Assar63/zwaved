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

The introspection should list twenty methods (`AddNode`, `StopAddNode`,
`RemoveNode`, `StopRemoveNode`, `RemoveFailedNode`, `SetSwitchBinary`,
`GetSwitchBinary`, `SetBasic`, `GetBasic`, `GetNodes`, `GetDongleInfo`,
`GetInitData`, `SetAssociation`, `RemoveAssociation`, `GetAssociation`,
`GetAssociationGroupings`, `SetMultichannelAssociation`,
`RemoveMultichannelAssociation`, `GetMultichannelAssociation`,
`GetMultichannelAssociationGroupings`) and
eleven signals (`NodeInclusionStatus`, `NodeExclusionStatus`,
`DongleStatus`, `DongleInfo`, `InitData`, `SendDataStatus`,
`ApplicationCommand`, `SwitchBinaryReport`, `AssociationReport`,
`AssociationGroupingsReport`, `RemoveFailedNodeStatus`).

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
| `SetSwitchBinary`                     | `y b y` (nodeId, on, callbackId)                                                                                                                   | Send a Binary Switch SET (CC 0x25) to a node; completion arrives as `SendDataStatus(callbackId, txStatus)`                                                                   |
| `GetSwitchBinary`                     | `y y` (nodeId, callbackId)                                                                                                                         | Send a Binary Switch GET; the node's reply lands as a typed `SwitchBinaryReport(sourceNodeId, state)` signal alongside the raw `ApplicationCommand`                          |
| `SetBasic`                            | `y y y` (nodeId, value, callbackId)                                                                                                                | Send a Basic SET (CC 0x20) — `value=0` off, `value=0xFF` on, `value=1..99` (`0x01..0x63`) dimmer level. Universal fallback for devices without a specific CC                 |
| `GetBasic`                            | `y y` (nodeId, callbackId)                                                                                                                         | Send a Basic GET; the node's reply lands as a raw `ApplicationCommand` signal carrying the Basic Report (`ccData[0] == 0x20`, `ccData[1] == 0x03`)                           |
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

Both are visible in `busctl --system monitor com.tiunda.ZWaved`. Use
the typed signal when you only care about Binary Switch state changes;
use the raw signal when you need to handle arbitrary CCs.

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
clients that connect later.

```bash
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 GetDongleInfo
```

If a dongle has not yet been introspected (none plugged in since the
daemon started), `GetDongleInfo` returns an empty struct (empty
`libraryVersion`, all bytes zero); clients should treat that as
"not available yet" and wait for the next `DongleInfo` signal.

## 17. Future: ubus

A second backend implementing the same methods/signals over OpenWrt's
ubus is on the roadmap. The CMake cache option `ZWAVED_EXTERNAL_API`
already accepts `ubus` and `both`, but only `dbus` is implemented today.
