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
- [ ] **Per-node protocol info** — call `FUNC_ID_GET_NODE_PROTOCOL_INFO` (0x41) for each seeded node so the registry's device-class triple and CC list survive daemon restarts too (today seeded entries have those fields zeroed until the node is re-included).
- [ ] **Node info** — detailed per-node view (capabilities, status, recent activity).
- [ ] **Network info** — aggregate view (node count, statuses, activity).
- [x] **Network status** — `GetNetworkStatus() → (b s s y u b y y t)` aggregates dongle connection, home ID, included-node count, in-flight session, and daemon uptime. Driven by a new retained `MessageBus::SessionStatus` event from `ProtocolThread` and the existing cached `DongleStatus` / `DongleInfo` / `NodeListChanged`. Errors continue to flow through Logger / journald — a structured error feed is a follow-up.
- [ ] **Network reset** — wipe and reinitialize the network.
- [x] **Remove failed node** — `RemoveFailedNode` over D-Bus drives `FUNC_ID_ZW_REMOVE_FAILED_NODE_ID` (0x61); response + callback emitted as `RemoveFailedNodeStatus` and the registry trims on success.
- [x] **Auto-lifeline on inclusion** — when a freshly-included node advertises `COMMAND_CLASS_ZWAVEPLUS_INFO` + `COMMAND_CLASS_ASSOCIATION` in its supported list, the protocol thread queues `SetAssociation(group=1, members=[controllerNodeId])` itself at the terminal step of inclusion.

### Command classes

- [ ] Implement remaining classes from [zwave specifications_3828_1.pdf](docs/zwave%20specifications_3828_1.pdf) as real devices land on the bench.
- [ ] **Virtual nodes** — simulate nodes for testing and integrations.
- [ ] **Scene controller thread** — multi-node scene/mode orchestration inside the daemon.

### Persistence & configuration

- [x] **SQLite-backed node registry** — `nodes.db` at `${ZWAVED_STATE_DIR:-/var/lib/zwaved}` keeps the included-node list across daemon restarts. Rows keyed by `(home_id, node_id)` so swapping dongles between networks is safe. Static info only.
- [ ] Persist additional network metadata (per-node dynamic state, association groups, configuration parameters).
- [x] **Configuration file** — `src/config/` parses `${ZWAVED_CONFIG:-/etc/zwaved/zwaved.conf}` (TOML, via `tomlplusplus`). Sections wired today: `[logger] min_level`, `[storage] state_dir`, `[[dongles.accept]]` (replaces hardcoded VID/PID), `[behavior] auto_lifeline`. Sample at `etc/zwaved.conf`.

### Observability

- [x] **Async logger** — `src/logger/` runs an MPSC queue + dedicated `ZWaveLog` consumer thread (constructor priority 101). Producers never block on I/O. Sink picked at build time via `ZWAVED_LOGGER_SINK`: `stdout` (default — captured by journald under systemd) or `syslog` (for OpenWRT / non-systemd hosts). Migration of existing `std::cout` / `std::cerr` call sites is incremental.

### Hardware

- [ ] Support more USB-dongle vendors beyond the Aeotec Z-Stick Gen5.
- [ ] Multi-dongle: drive several controllers from one daemon.

### Quality & docs

- [x] **Unit tests** — five modules covered: `HostApi` (encoder + decoder round-trips for every `FUNC_ID` the daemon issues), `BinarySwitch` (encode + decodeReport), `Association` (encode + decode for both Report and GroupingsReport), `MultichannelAssociation` (encode/decode for whole-node + endpoint-pair members, REMOVE-all elision of MARKER, malformed-frame rejection), `FrameTransport` (passive `pumpOnce` paths plus active `sendRequest` happy / NAK-retry / CAN-retry paths, driven over a `socketpair(2)` via `SerialPort::adoptFd`). GoogleTest via `libgtest-dev`; `ZWAVED_BUILD_TESTS=ON` default; 76/76 in ~2.5s via `ctest --test-dir cmake-build-gnu`.
- [ ] `Help` command — list of supported D-Bus methods and what they do.
- [ ] Refresh `MANUAL.md` and `README.md`; add a dedicated README for `utils/zwave-terminal/`.

---

## External APIs

### D-Bus

- [ ] Full D-Bus introspection / property browsing for the existing interface.

### UBUS

- [ ] Second `external-api` backend over UBUS for OpenWrt and other constrained environments.

### MQTT

- [ ] Third `external-api` backend over an MQTT broker for remote / centralized control.

---

## zwave-terminal client

### Display

- [ ] **Help** window — available commands and descriptions.
- [ ] **Logs** window — live daemon log stream.
- [ ] **Settings** window — log level, connection, preferences.
- [x] **Node list** — `[l]` fetches `GetNodes` and renders each node into the activity pane.
- [ ] **Node list (rich)** — dedicated window with live state column instead of a one-shot dump.
- [ ] **Node info** — drill-down per node.
- [ ] **Network info** — overview of the network.
- [x] **Network status** — `[n]` calls `GetNetworkStatus` and renders the aggregate (dongle, home ID, node count, active session, uptime).

### Control

- [x] **Binary Switch control** — `[3] ON` / `[4] OFF` prompt for node ID and issue `SetSwitchBinary`; activity pane logs `SendDataStatus` decode and unsolicited `SwitchBinaryReport`.
- [x] **Remove failed node** — `[f]` prompts for a node ID and issues `RemoveFailedNode`; activity pane decodes the `RemoveFailedNodeStatus` response + result phases.
- [ ] **Node control: other CCs** — multilevel switch, thermostat, door lock, etc.
- [ ] **Scene control** — trigger predefined multi-node scenes.

---

## Product / packaging

- [ ] Installation scripts for Ubuntu and Raspberry Pi.
- [ ] Build-time configuration for which interfaces (D-Bus / UBUS / MQTT / utils) are included.
