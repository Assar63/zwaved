# TODO

Working notes on what's coming next for the zwaved daemon, the
companion `zwave-terminal` client, and packaging.

> Legend: `[ ]` open · `[x]` done

---

## zwaved daemon

### Network & node operations

- [x] **Binary Switch (CC `0x25`)** — `SetSwitchBinary` over D-Bus, end-to-end including SendData and Report decode.
- [x] **Association (CC `0x85`)** — `SetAssociation` / `RemoveAssociation` / `GetAssociation` / `GetAssociationGroupings`; `AssociationReport` and `AssociationGroupingsReport` signals on the unsolicited path.
- [x] **Multi Channel Association (CC `0x8E`)** — `SetMultichannelAssociation` / `RemoveMultichannelAssociation` / `GetMultichannelAssociation` / `GetMultichannelAssociationGroupings`. Same six wire commands as plain Association, but each group also carries `(nodeId, endpoint)` pairs after a `MARKER = 0x00` byte. Reports come back as raw `ApplicationCommand` signals — clients filter on `ccData[0] == 0x8E`.
- [x] **Unsolicited event handling** — `FUNC_ID_APPLICATION_COMMAND_HANDLER` decoded and fanned out via `MessageBus`.
- [x] **Node list** — `src/node-registry/` tracks included nodes; exposed as `GetNodes()` on D-Bus and via `[l]` in the terminal.
- [x] **Dongle introspection on connect** — `GET_VERSION` + `MEMORY_GET_ID` synchronously after serial open; published as `DongleInfo` (retained on the bus, signalled on D-Bus, cached for `GetDongleInfo`). `[i]` in the terminal queries it.
- [x] **Existing-network discovery** — `FUNC_ID_SERIAL_API_GET_INIT_DATA` (0x02) runs at startup; node bitmap is expanded and seeded into `NodeRegistry`. Full payload exposed as `GetInitData()` D-Bus method + `InitData` signal.
- [x] **Per-node device class** — `FUNC_ID_GET_NODE_PROTOCOL_INFO` (0x41) runs at startup for every seeded node and fills `basic`/`generic`/`specific` on each entry. The query is controller-local (no on-air traffic), so it works for sleeping or out-of-range nodes too. Persistence goes through the existing UPSERT.
- [x] **Per-node CC list (FUNC_ID_REQUEST_NODE_INFO `0x60`)** — `RequestNodeInfo(nodeId, sessionId)` over D-Bus drives the dongle to ask the node for its NodeInformation Frame. The synchronous 1-byte Response comes back as the `RequestNodeInfoStatus` signal (`accepted` bool, tagged with the caller's sessionId). The async reply arrives as `FUNC_ID_APPLICATION_UPDATE` (0x49) and is republished as the `NodeInfoUpdate` signal — which also fires for unsolicited NIFs from waking nodes. On `STATUS_NODE_INFO_RECEIVED` the daemon updates NodeRegistry's device-class triple AND command-class list (new `NodeRegistry::updateCommandClasses` helper). Best-effort and lazy by design — not driven at startup since sleeping nodes would block.
- [x] **Network status** — `GetNetworkStatus() → (b s s y u b y y t)` aggregates dongle connection, home ID, included-node count, in-flight session, and daemon uptime. Driven by a new retained `MessageBus::SessionStatus` event from `ProtocolThread` and the existing cached `DongleStatus` / `DongleInfo` / `NodeListChanged`. Errors continue to flow through Logger / journald — a structured error feed is a follow-up.
- [ ] [Network reset](https://github.com/Assar63/zwaved/issues/2)
- [x] **Remove failed node** — `RemoveFailedNode` over D-Bus drives `FUNC_ID_ZW_REMOVE_FAILED_NODE_ID` (0x61); response + callback emitted as `RemoveFailedNodeStatus` and the registry trims on success.
- [x] **Auto-lifeline on inclusion** — when a freshly-included node advertises `COMMAND_CLASS_ZWAVEPLUS_INFO` + `COMMAND_CLASS_ASSOCIATION` in its supported list, the protocol thread queues `SetAssociation(group=1, members=[controllerNodeId])` itself at the terminal step of inclusion.
- [ ] [Periodic neighbor refresh (FUNC_ID_ZW_REQUEST_NODE_NEIGHBOR_UPDATE 0x48)](https://github.com/Assar63/zwaved/issues/3)
- [ ] [Multicast SendData (FUNC_ID_ZW_SEND_DATA_MULTI 0x14)](https://github.com/Assar63/zwaved/issues/4)
- [ ] [Controller backup/restore (FUNC_ID_NVM_BACKUP_RESTORE 0xF8)](https://github.com/Assar63/zwaved/issues/5)
- [ ] [Per-node liveness](https://github.com/Assar63/zwaved/issues/6)

### Command classes

**Simple — single fixed-shape payload, mirrors the existing BinarySwitch / Association pattern:**

- [x] **Basic (CC `0x20`)** — `SetBasic` / `GetBasic` over D-Bus drive Basic SET / GET; the codec also decodes both v1 (3-byte) and v2+ (5-byte with `targetValue` + `duration`) Reports. Reports come back through the existing `ApplicationCommand` signal — clients filter on `ccData[0] == 0x20`.
- [x] **Multilevel Switch (CC `0x26`)** — `SetMultilevelSwitch` (with v2+ `duration` byte) / `GetMultilevelSwitch` over D-Bus; codec decodes both v1 (3-byte) and v2+ (5-byte with `targetValue` + `duration`) Reports. Reports come back through the existing `ApplicationCommand` signal — clients filter on `ccData[0] == 0x26`.
- [x] **Battery (CC `0x80`)** — `GetBattery` over D-Bus drives Battery GET; the codec decodes Reports and surfaces both the raw `level` byte (0..100 % or `0xFF` low-battery sentinel) and a derived `lowBattery` bool. Reports come back through the typed `BatteryReport` D-Bus signal (and the raw `ApplicationCommand` signal); the v2 charging / health bitfields are ignored today.
- [ ] [Version (CC 0x86)](https://github.com/Assar63/zwaved/issues/9)
- [x] **Manufacturer Specific (CC `0x72`)** — `GetManufacturerSpecific` over D-Bus drives Manufacturer Specific GET; the codec decodes the v1 Report (CC + cmd + three big-endian u16s) and surfaces `manufacturerId` / `productTypeId` / `productId`. Reports come back through the typed `ManufacturerSpecificReport` D-Bus signal (and the raw `ApplicationCommand` signal); v2 Device Specific Get/Report is a follow-up.
- [x] **Z-Wave Plus Info (CC `0x5E`)** — `GetZWavePlusInfo` over D-Bus drives Z-Wave Plus Info GET (note: GET=0x01, REPORT=0x02 — opposite of the more common 0x02/0x03 layout). The codec decodes the 9-byte v2 Report and surfaces `zwavePlusVersion` + `roleType` (0x00..0x07: central/sub/portable controller, always-on/sleeping slave, …) + `nodeType` (0x00 node, 0x02 IP gateway) + two big-endian u16 icon types (installer + user). Reports come back through the typed `ZWavePlusInfoReport` D-Bus signal.
- [ ] [Indicator (CC 0x87)](https://github.com/Assar63/zwaved/issues/12)

**More involved — variable-shape payloads, encapsulation, or multi-frame state machines:**

- [ ] [Multi Channel (CC 0x60)](https://github.com/Assar63/zwaved/issues/13)
- [ ] [Supervision (CC 0x6C)](https://github.com/Assar63/zwaved/issues/14)
- [ ] [Wake Up (CC 0x84)](https://github.com/Assar63/zwaved/issues/15)
- [ ] [Configuration (CC 0x70)](https://github.com/Assar63/zwaved/issues/16)
- [ ] [Sensor Multilevel (CC 0x31)](https://github.com/Assar63/zwaved/issues/17)
- [ ] [Sensor Binary (CC 0x30)](https://github.com/Assar63/zwaved/issues/18)
- [ ] [Notification (CC 0x71)](https://github.com/Assar63/zwaved/issues/19)
- [ ] [Meter (CC 0x32)](https://github.com/Assar63/zwaved/issues/20)
- [ ] [Color Switch (CC 0x33)](https://github.com/Assar63/zwaved/issues/21)
- [ ] [Central Scene (CC 0x5B)](https://github.com/Assar63/zwaved/issues/22)
- [ ] [Thermostat HVAC quartet (CC 0x40 Mode / 0x43 Setpoint / 0x42 Operating State / 0x44 Fan Mode)](https://github.com/Assar63/zwaved/issues/23)
- [ ] [Door Lock (CC 0x62) + User Code (CC 0x63)](https://github.com/Assar63/zwaved/issues/24)
- [ ] [Transport Service (CC 0x55)](https://github.com/Assar63/zwaved/issues/25)

**Security — encrypted transport. Each of these is an order of magnitude more work than the simple CCs combined; treat as a dedicated epic:**

- [ ] [Security S0 (CC 0x98)](https://github.com/Assar63/zwaved/issues/26)
- [ ] [Security S2 (CC 0x9F)](https://github.com/Assar63/zwaved/issues/27)
- [ ] [CRC-16 Encapsulation (CC 0x56)](https://github.com/Assar63/zwaved/issues/28)

- [ ] [Virtual nodes](https://github.com/Assar63/zwaved/issues/29)
- [ ] [Scene controller thread](https://github.com/Assar63/zwaved/issues/30)

### Persistence & configuration

- [x] **SQLite-backed node registry** — `nodes.db` at `${ZWAVED_STATE_DIR:-/var/lib/zwaved}` keeps the included-node list across daemon restarts. Rows keyed by `(home_id, node_id)` so swapping dongles between networks is safe. Static info only.
- [x] **Configuration file** — `src/config/` parses `${ZWAVED_CONFIG:-/etc/zwaved/zwaved.conf}` with a hand-rolled INI-flavoured key/value parser (no third-party dependency). Sections wired today: `[logger] min_level`, `[storage] state_dir`, `[dongles] accept = vid:pid:name` (one row per dongle, replaces hardcoded VID/PID), `[behavior] auto_lifeline`. Sample at `etc/zwaved.conf`.
- [ ] [SIGHUP-driven config reload](https://github.com/Assar63/zwaved/issues/31)

### Observability

- [x] **Async logger** — `src/logger/` runs an MPSC queue + dedicated `ZWaveLog` consumer thread (constructor priority 101). Producers never block on I/O. Sink picked at build time via `ZWAVED_LOGGER_SINK`: `stdout` (default — captured by journald under systemd) or `syslog` (for OpenWRT / non-systemd hosts). Migration of existing `std::cout` / `std::cerr` call sites is incremental.
- [x] **Structured error feed** — retained `MessageBus::DaemonError` event (severity / source / code / message) re-emitted as a typed `DaemonError` D-Bus signal. Lets external clients react to "dongle disconnected" / "DBus bind failed" / "no external API backend" by code rather than grep-mining journald. Publish sites today: serial-port open failure, dongle introspection timeouts (GET_VERSION / MEMORY_GET_ID), udev init failures, D-Bus bind failure, missing external-API backend, config-file open failure. Recovery is modelled by publishing a default-constructed `DaemonError{}` — currently done after successful dongle introspection. Severity values align with `Logger::Level` plus a `SEVERITY_CRITICAL` step; codes are byte-grouped by source-module high-nibble.
- [ ] [systemd Type=notify integration](https://github.com/Assar63/zwaved/issues/33)

### Hardware

- [x] **Support more USB-dongle vendors beyond the Aeotec Z-Stick Gen5** — handled by the `[dongles] accept = vid:pid:name` config entry; any stick speaking the same Serial API can be added at deploy time without a recompile (the sample at `etc/zwaved.conf` already shows a commented-out Aeotec Z-Stick 7 row). New chip families (different FUNC_IDs / chip-type byte) are tracked separately as the 700-series and 800-series silicon entries below.
- [ ] [700-series silicon support](https://github.com/Assar63/zwaved/issues/34)
- [ ] [Multi-dongle: drive several controllers from one daemon](https://github.com/Assar63/zwaved/issues/35)

### Dependency hygiene

- [x] **CI build green** — `docker/Dockerfile` fixed in `1e69248`: added `make` to the toolchain apt list; switched sdbus-c++ from `git clone --depth 1 master` (which silently tracked upstream 2.x's renamed API) to `libsdbus-c++-dev` from apt (1.4.x, matches dev); pinned eventpp to `v0.1.3` via an `EVENTPP_TAG` build-arg. First green build since the workflow was introduced.
- [ ] [docs: inventory dependencies + versions in docs/DEPENDENCIES.md](https://github.com/Assar63/zwaved/issues/60)
- [ ] [ci: durable pinning policy for third-party dependencies](https://github.com/Assar63/zwaved/issues/61)
- [ ] [deps: documented upgrade path for pinned dependencies](https://github.com/Assar63/zwaved/issues/62)
- [ ] [ci: scheduled deps-check workflow surfaces drift from upstream](https://github.com/Assar63/zwaved/issues/63)

### Quality & docs

- [x] **Unit tests** — six modules covered: `HostApi` (encoder + decoder round-trips for every `FUNC_ID` the daemon issues), `BinarySwitch` (encode + decodeReport), `Basic` (encode SET / GET + decode v1 3-byte and v2+ 5-byte Reports), `Association` (encode + decode for both Report and GroupingsReport), `MultichannelAssociation` (encode/decode for whole-node + endpoint-pair members, REMOVE-all elision of MARKER, malformed-frame rejection), `FrameTransport` (passive `pumpOnce` paths plus active `sendRequest` happy / NAK-retry / CAN-retry paths, driven over a `socketpair(2)` via `SerialPort::adoptFd`). GoogleTest via `libgtest-dev`; `ZWAVED_BUILD_TESTS=ON` default; 91/91 in ~2.5s via `ctest --test-dir cmake-build-gnu`.
- [ ] [Refresh MANUAL.md, README.md, and add a dedicated README for utils/zwave-terminal/](https://github.com/Assar63/zwaved/issues/36)

---

## External APIs

### D-Bus

- [ ] [D-Bus property surface for cached state](https://github.com/Assar63/zwaved/issues/37)
- [ ] [Per-method D-Bus authorization](https://github.com/Assar63/zwaved/issues/38)

### UBUS

- [ ] [UBUS external-API backend](https://github.com/Assar63/zwaved/issues/39)

### MQTT

- [ ] [MQTT external-API backend](https://github.com/Assar63/zwaved/issues/40)

---

## zwave-terminal client

### Display

- [ ] [Help window (zwave-terminal)](https://github.com/Assar63/zwaved/issues/41)
- [ ] [Logs window (zwave-terminal)](https://github.com/Assar63/zwaved/issues/42)
- [ ] [Settings window (zwave-terminal)](https://github.com/Assar63/zwaved/issues/43)
- [x] **Node list** — `[l]` fetches `GetNodes` and renders each node into the activity pane.
- [ ] [Node list (rich)](https://github.com/Assar63/zwaved/issues/44)
- [ ] [Node info window](https://github.com/Assar63/zwaved/issues/45)
- [ ] [Network info window](https://github.com/Assar63/zwaved/issues/46)
- [x] **Network status** — `[n]` calls `GetNetworkStatus` and renders the aggregate (dongle, home ID, node count, active session, uptime).

### Control

- [x] **Binary Switch control** — `[3] ON` / `[4] OFF` prompt for node ID and issue `SetSwitchBinary`; activity pane logs `SendDataStatus` decode and unsolicited `SwitchBinaryReport`.
- [x] **Remove failed node** — `[f]` prompts for a node ID and issues `RemoveFailedNode`; activity pane decodes the `RemoveFailedNodeStatus` response + result phases.
- [ ] [Node control for non-binary CCs](https://github.com/Assar63/zwaved/issues/47)
- [ ] [Scene control (zwave-terminal)](https://github.com/Assar63/zwaved/issues/48)

---

## Future / unlikely

Tracked for completeness; each item below is either niche, very large,
or both, and has no concrete bench device or use case demanding it
today. Move an item up into the main sections if a real need shows up.

- [ ] [OTA firmware update of nodes (Firmware Update Meta Data CC 0x7A)](https://github.com/Assar63/zwaved/issues/49)
- [ ] [OTW firmware update of the dongle itself](https://github.com/Assar63/zwaved/issues/50)
- [ ] [800-series silicon + Z-Wave Long Range](https://github.com/Assar63/zwaved/issues/51)
- [ ] [Audit log of administrative D-Bus calls](https://github.com/Assar63/zwaved/issues/52)
- [ ] [Metrics / Prometheus exporter](https://github.com/Assar63/zwaved/issues/53)
- [ ] [Fuzzing the frame parser and ApplicationCommand decoder](https://github.com/Assar63/zwaved/issues/54)

---

## Product / packaging

- [ ] [Installation scripts for Ubuntu and Raspberry Pi](https://github.com/Assar63/zwaved/issues/55)
- [ ] [Build-time configuration for which interfaces are included](https://github.com/Assar63/zwaved/issues/56)
