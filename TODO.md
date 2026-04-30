# TODO

Working notes on what's coming next for the zwaved daemon, the
companion `zwave-terminal` client, and packaging.

> Legend: `[ ]` open · `[x]` done

---

## zwaved daemon

### Network & node operations

- [x] **Binary Switch (CC `0x25`)** — `SetSwitchBinary` over D-Bus, end-to-end including SendData and Report decode.
- [x] **Unsolicited event handling** — `FUNC_ID_APPLICATION_COMMAND_HANDLER` decoded and fanned out via `MessageBus`.
- [x] **Node list** — `src/node-registry/` tracks included nodes; exposed as `GetNodes()` on D-Bus and via `[l]` in the terminal.
- [x] **Dongle introspection on connect** — `GET_VERSION` + `MEMORY_GET_ID` synchronously after serial open; published as `DongleInfo` (retained on the bus, signalled on D-Bus, cached for `GetDongleInfo`). `[i]` in the terminal queries it.
- [ ] **Existing-network discovery** — `FUNC_ID_SERIAL_API_GET_INIT_DATA` (0x02) at startup to populate `NodeRegistry` from dongle memory so node list survives daemon restarts.
- [ ] **Node info** — detailed per-node view (capabilities, status, recent activity).
- [ ] **Network info** — aggregate view (node count, statuses, activity).
- [ ] **Network status** — current health, ongoing operations, errors.
- [ ] **Network reset** — wipe and reinitialize the network.

### Command classes

- [ ] Implement remaining classes from [zwave specifications_3828_1.pdf](docs/zwave%20specifications_3828_1.pdf) as real devices land on the bench.
- [ ] **Virtual nodes** — simulate nodes for testing and integrations.
- [ ] **Scene controller thread** — multi-node scene/mode orchestration inside the daemon.

### Persistence & configuration

- [ ] Local database for nodes and network metadata _(may force a structural re-layout)_.
- [ ] Configuration file for daemon settings.

### Observability

- [ ] **Lock-free async logger** — thread-safe event/error logging that does not block producers.

### Hardware

- [ ] Support more USB-dongle vendors beyond the Aeotec Z-Stick Gen5.
- [ ] Multi-dongle: drive several controllers from one daemon.

### Quality & docs

- [ ] Unit tests.
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
- [ ] **Network status** — health and error summary.

### Control

- [x] **Binary Switch control** — `[3] ON` / `[4] OFF` prompt for node ID and issue `SetSwitchBinary`; activity pane logs `SendDataStatus` decode and unsolicited `SwitchBinaryReport`.
- [ ] **Node control: other CCs** — multilevel switch, thermostat, door lock, etc.
- [ ] **Scene control** — trigger predefined multi-node scenes.

---

## Product / packaging

- [ ] Installation scripts for Ubuntu and Raspberry Pi.
- [ ] Build-time configuration for which interfaces (D-Bus / UBUS / MQTT / utils) are included.
