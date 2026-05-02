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
- [ ] **Per-node CC list** — follow-up to the device-class fill-in above. The supported CC list comes from `FUNC_ID_REQUEST_NODE_INFO` (0x60 in serial-API space, distinct from the `Multi Channel` CC), which triggers the node to send a NIF over the air; the reply arrives asynchronously as `FUNC_ID_APPLICATION_UPDATE` (0x49). Best-effort and lazy — driving it at startup would block on every sleeping node.
- [x] **Network status** — `GetNetworkStatus() → (b s s y u b y y t)` aggregates dongle connection, home ID, included-node count, in-flight session, and daemon uptime. Driven by a new retained `MessageBus::SessionStatus` event from `ProtocolThread` and the existing cached `DongleStatus` / `DongleInfo` / `NodeListChanged`. Errors continue to flow through Logger / journald — a structured error feed is a follow-up.
- [ ] **Network reset** — wipe and reinitialize the network.
- [x] **Remove failed node** — `RemoveFailedNode` over D-Bus drives `FUNC_ID_ZW_REMOVE_FAILED_NODE_ID` (0x61); response + callback emitted as `RemoveFailedNodeStatus` and the registry trims on success.
- [x] **Auto-lifeline on inclusion** — when a freshly-included node advertises `COMMAND_CLASS_ZWAVEPLUS_INFO` + `COMMAND_CLASS_ASSOCIATION` in its supported list, the protocol thread queues `SetAssociation(group=1, members=[controllerNodeId])` itself at the terminal step of inclusion.
- [ ] **Periodic neighbor refresh** — drive `FUNC_ID_ZW_REQUEST_NODE_NEIGHBOR_UPDATE` (0x48) on a schedule (or on user demand). Without it, mesh routing degrades quietly as devices move or drop and the operator has no signal that anything is wrong.
- [ ] **Multicast SendData** — `FUNC_ID_ZW_SEND_DATA_MULTI` (0x14). Once associations and groups are in regular use, hitting members one-by-one is wasteful; multicast collapses an N-node group hit into one frame.
- [ ] **Controller backup/restore** — `FUNC_ID_NVM_BACKUP_RESTORE` (0xF8). Highest-blast-radius missing feature: a dead dongle today means rebuilding the whole Z-Wave network from scratch. Backup pairs naturally with a periodic export to `${ZWAVED_STATE_DIR}` and a `RestoreNetwork` D-Bus method gated behind operator confirmation.
- [ ] **Per-node liveness** — track last-seen timestamp + recent TX-status history per node and expose it on `GetNodes` / a `GetNodeHealth(nodeId)` method. `GetNodes` today answers "who's been included," not "who's actually reachable" — a battery-powered sensor going silent is invisible until the operator notices the application data has stopped.

### Command classes

**Simple — single fixed-shape payload, mirrors the existing BinarySwitch / Association pattern:**

- [x] **Basic (CC `0x20`)** — `SetBasic` / `GetBasic` over D-Bus drive Basic SET / GET; the codec also decodes both v1 (3-byte) and v2+ (5-byte with `targetValue` + `duration`) Reports. Reports come back through the existing `ApplicationCommand` signal — clients filter on `ccData[0] == 0x20`.
- [ ] **Multilevel Switch (CC `0x26`)** — Set / Get / Report; 0–99 level + duration byte. Dimmers, blinds, fan speed.
- [ ] **Battery (CC `0x80`)** — Get / Report; one byte (0–100 %, `0xFF` = low). Prerequisite for useful sleeping-device support.
- [ ] **Version (CC `0x86`)** — Get / Report; library / protocol / app version bytes. Per-node analogue of the existing `DongleInfo` story.
- [ ] **Manufacturer Specific (CC `0x72`)** — Get / Report; manufacturer / product-type / product ID (three uint16s).
- [ ] **Z-Wave Plus Info (CC `0x5E`)** — Get / Report; role type + install type + icon IDs. The 0x5E byte is already inspected during inclusion to gate auto-lifeline; lifting that into a real codec exposes the device-class metadata to clients.
- [ ] **Indicator (CC `0x87`)** — Set / Get / Report on the LED indicator. v1 is a single byte; v3+ adds property-id structures (would land as the simple v1 first).

**More involved — variable-shape payloads, encapsulation, or multi-frame state machines:**

- [ ] **Multi Channel (CC `0x60`)** — endpoint encapsulation. Wraps another CC's payload in `(sourceEp, destEp, encapCcData)`. Prerequisite for actually *driving* endpoints reached via the existing `MultichannelAssociation` work; today we can configure the association list but can't address the endpoint itself.
- [ ] **Supervision (CC `0x6C`)** — explicit acknowledgement encapsulation for SET commands. Wraps a CC frame, expects a Supervision Report back with `status` and `more updates follow`. Lets the daemon distinguish "transmitted" (current `SendDataStatus`) from "applied" (the node actually obeyed).
- [ ] **Wake Up (CC `0x84`)** — sleeping-node lifecycle. Get/Set/Report on the wake-up interval (3-byte seconds + 1-byte node ID), plus the Wake Up Notification (cmd `0x07`) push from the node. Needs a queue layer in `ProtocolThread` so commands aimed at a sleeping node sit until the next Wake Up Notification, then flush before No-More-Information.
- [ ] **Configuration (CC `0x70`)** — vendor-specific parameter store. v1 had fixed widths; v4 adds variable 1/2/4-byte values and a default-value flag. Needs per-device width metadata to round-trip values correctly; pairs naturally with **Manufacturer Specific** discovery.
- [ ] **Sensor Multilevel (CC `0x31`)** — temperature, humidity, luminance, etc. Payload is a sensor-type byte, a precision/scale/size flag byte, then a 1/2/4-byte signed integer. Multiple scales per sensor type (e.g. °C / °F).
- [ ] **Sensor Binary (CC `0x30`)** — door/window contact, motion. Officially deprecated by Notification but still present on virtually every legacy sensor.
- [ ] **Notification (CC `0x71`)** — push events from sensors (motion, water leak, smoke, tamper, …). Variable-length parameter payload keyed by `(notification type, event)`. The right home for "node X reports motion" today.
- [ ] **Meter (CC `0x32`)** — energy, water, gas. Multi-rate-type, multi-scale, includes delta-time-since-previous and previous-value fields. Common on smart plugs.
- [ ] **Color Switch (CC `0x33`)** — multi-component RGB/RGBW/CCT lighting. Set/Get/Report carry a variable-length list of `(componentId, value)` pairs.
- [ ] **Central Scene (CC `0x5B`)** — scene IDs from physical button presses (single/double/triple/hold/release) on remotes and wall switches. Notification (cmd `0x03`) is push-only.
- [ ] **Thermostat Mode (CC `0x40`)** / **Setpoint (CC `0x43`)** / **Operating State (CC `0x42`)** / **Fan Mode (CC `0x44`)** — HVAC quartet. Setpoint payload mirrors Sensor Multilevel's precision/scale/size encoding for temperatures.
- [ ] **Door Lock (CC `0x62`)** + **User Code (CC `0x63`)** — locks. Door Lock has a multi-byte mode/condition state; User Code carries variable-length PIN strings keyed by slot.
- [ ] **Transport Service (CC `0x55`)** — segmentation for frames longer than the radio MTU. Required once we hit large payloads (multi-byte Configuration values, long User Code lists, big Notification parameters).

**Security — encrypted transport. Each of these is an order of magnitude more work than the simple CCs combined; treat as a dedicated epic:**

- [ ] **Security S0 (CC `0x98`)** — first-generation encrypted command encapsulation (AES-128 CBC + CMAC). Single network key shared by every secure node. Each outgoing encrypted frame requires a `Nonce Get` / `Nonce Report` round-trip first, so the protocol layer needs an inline pause-and-wait state. Inclusion gains a `Scheme Get / Report → Network Key Set / Verify` handshake that has to run inside the existing inclusion session before the daemon emits the "complete" status. Required for legacy door locks and garage door controllers.
- [ ] **Security S2 (CC `0x9F`)** — second-generation encrypted encapsulation (AES-128 CCM with multiple class keys: Unauthenticated, Authenticated, Access Control). Mandatory for certification on all post-2017 devices. Inclusion is DSK-driven and the handshake spans `KEX Get / Report / Set` (negotiate scheme + curve), `Public Key Report A/B` (ECDH key exchange — operator confirms the DSK pin), then `Network Key Get / Report / Verify` once per granted key class. Day-to-day traffic uses Singlecast Pre-Agreed Nonces (SPAN) with a per-peer counter — losing or rotating a SPAN forces a `Nonce Sync`. Multicast adds MPAN (Multicast PAN) on top of SPAN.
- [ ] **CRC-16 Encapsulation (CC `0x56`)** — pre-S0 integrity wrapper. Verify-only on receive in practice; used by some legacy non-secure devices that still want frame-level checksum redundancy beyond the serial-API XOR. Cheap to add but not load-bearing for new networks.

- [ ] **Virtual nodes** — simulate nodes for testing and integrations.
- [ ] **Scene controller thread** — multi-node scene/mode orchestration inside the daemon.

### Persistence & configuration

- [x] **SQLite-backed node registry** — `nodes.db` at `${ZWAVED_STATE_DIR:-/var/lib/zwaved}` keeps the included-node list across daemon restarts. Rows keyed by `(home_id, node_id)` so swapping dongles between networks is safe. Static info only.
- [x] **Configuration file** — `src/config/` parses `${ZWAVED_CONFIG:-/etc/zwaved/zwaved.conf}` (TOML, via `tomlplusplus`). Sections wired today: `[logger] min_level`, `[storage] state_dir`, `[[dongles.accept]]` (replaces hardcoded VID/PID), `[behavior] auto_lifeline`. Sample at `etc/zwaved.conf`.
- [ ] **SIGHUP-driven reload** — re-read `${ZWAVED_CONFIG}` on `SIGHUP` and republish the four retained Config events on `MessageBus`. Subscribers already pick the new values up via replay-on-subscribe, so the work is mostly: handler, file re-parse, error path that keeps the previous values on parse failure.

### Observability

- [x] **Async logger** — `src/logger/` runs an MPSC queue + dedicated `ZWaveLog` consumer thread (constructor priority 101). Producers never block on I/O. Sink picked at build time via `ZWAVED_LOGGER_SINK`: `stdout` (default — captured by journald under systemd) or `syslog` (for OpenWRT / non-systemd hosts). Migration of existing `std::cout` / `std::cerr` call sites is incremental.
- [ ] **Structured error feed** — alluded to in the `GetNetworkStatus` entry; the actual feature is a retained `MessageBus::DaemonError` event (severity, source module, code, message) emitted as a typed D-Bus signal. Lets external clients react to "dongle disconnected" / "node N stopped responding" / "config reload failed" instead of grep-mining journald.
- [ ] **systemd `Type=notify` integration** — call `sd_notify(READY=1)` from the protocol thread once the dongle is open and introspected; periodic `WATCHDOG=1` pings tied to a healthy frame-transport pump. The unit file moves to `Type=notify` with `WatchdogSec=`. Buys automatic restart on dongle deadlock without polling stdout for keep-alive lines.

### Hardware

- [ ] Support more USB-dongle vendors beyond the Aeotec Z-Stick Gen5.
- [ ] **700-series silicon support** — Silicon Labs ZGM130S / EFR32-based sticks (Aeotec Z-Stick 7, Zooz ZST10 700, etc.). Same Serial API shape but new FUNC_IDs and a different chip-type byte; needs a small dispatch fork in `HostApi.cpp` rather than a fresh transport.
- [ ] Multi-dongle: drive several controllers from one daemon.

### Quality & docs

- [x] **Unit tests** — five modules covered: `HostApi` (encoder + decoder round-trips for every `FUNC_ID` the daemon issues), `BinarySwitch` (encode + decodeReport), `Association` (encode + decode for both Report and GroupingsReport), `MultichannelAssociation` (encode/decode for whole-node + endpoint-pair members, REMOVE-all elision of MARKER, malformed-frame rejection), `FrameTransport` (passive `pumpOnce` paths plus active `sendRequest` happy / NAK-retry / CAN-retry paths, driven over a `socketpair(2)` via `SerialPort::adoptFd`). GoogleTest via `libgtest-dev`; `ZWAVED_BUILD_TESTS=ON` default; 76/76 in ~2.5s via `ctest --test-dir cmake-build-gnu`.
- [ ] Refresh `MANUAL.md` and `README.md`; add a dedicated README for `utils/zwave-terminal/`.

---

## External APIs

### D-Bus

- [ ] Full D-Bus introspection / property browsing for the existing interface.
- [ ] **Per-method authorization** — today `dbus/com.tiunda.ZWaved.conf` is all-or-nothing: any peer who matches the policy can call any method. Once non-root callers reach the bus, "anyone can `RemoveNode`" is a footgun. Plumb sdbus-c++'s peer-credentials accessor into a small ACL layer so destructive methods (Add/Remove/RemoveFailed/Network reset) require an `operator` group while query methods stay open. PolicyKit integration is the obvious upgrade path if a UID-based check isn't enough.

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

## Future / unlikely

Tracked for completeness; each item below is either niche, very large,
or both, and has no concrete bench device or use case demanding it
today. Move an item up into the main sections if a real need shows up.

- [ ] **OTA firmware update of nodes** — Firmware Update Meta Data CC (`0x7A`). Multi-frame transfer of vendor-supplied firmware images, vendor-specific image formats, an integrity step, and a long resumption story when the link drops mid-flash. Big effort, niche payoff outside professional-installer settings.
- [ ] **OTW (over-the-wire) firmware update of the dongle itself** — separate code path from node OTA; uses the `FUNC_ID_ZW_FIRMWARE_UPDATE_NVM` family. Useful only if you actually ship dongle firmware; irrelevant when consuming a vendor stick.
- [ ] **800-series silicon + Z-Wave LR** — chip type `5` in the spec, new FUNC_IDs, 100 mW power output, and a separate Long Range PHY with its own inclusion paths. Worth picking up once an 800-series stick lands on the bench; until then there's no realistic way to test.
- [ ] **Audit log of administrative D-Bus calls** — capture peer UID + timestamp + method for inclusion / exclusion / removal calls into a separate append-only log. Pairs with the per-method authorization work but only matters in shared/operator settings; a single-user home install gets nothing out of it.
- [ ] **Metrics / Prometheus exporter** — protocol-thread RTT histograms, retry counts, ApplicationCommand throughput, dongle health. Nice for graphing, premature for a daemon that's still adding feature surface every week.
- [ ] **Fuzzing the frame parser and ApplicationCommand decoder** — Z-Wave is a wireless protocol; unfriendly bytes from the air shouldn't crash the daemon. The current unit-test set provides spot coverage; libFuzzer would broaden it. Low frequency of crash reports in practice keeps this off the critical path.

---

## Product / packaging

- [ ] Installation scripts for Ubuntu and Raspberry Pi.
- [ ] Build-time configuration for which interfaces (D-Bus / UBUS / MQTT / utils) are included.
