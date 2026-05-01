# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

```bash
# Configure and build with GCC 15 (outputs to cmake-build-gnu/)
cmake --preset gnu
cmake --build cmake-build-gnu

# Configure and build with LLVM/Clang 20 (outputs to cmake-build-llvm/)
cmake --preset llvm
cmake --build cmake-build-llvm

# Build with clang-tidy integrated — tidy errors fail the build, only changed files re-checked
cmake --preset gnu-tidy && cmake --build cmake-build-gnu-tidy
cmake --preset llvm-tidy && cmake --build cmake-build-llvm-tidy

# Run
./cmake-build-gnu/zwaved
./cmake-build-llvm/zwaved
```

Requires: GCC 15 (`g++-15`), LLVM/Clang 20 (`clang++-20`), CMake 3.20+, `libudev-dev`, `libsdbus-c++-dev` (pulls in `libsystemd-dev`), `libsqlite3-dev`, eventpp (header-only via `find_package`). C++26 standard.

The CMake cache option `ZWAVED_EXTERNAL_API` selects which external transports are built. Accepted values: `dbus` (default), `ubus` (placeholder, not implemented), `both`.

The CMake cache option `ZWAVED_LOGGER_SINK` selects the logger backend. Accepted values: `stdout` (default — picked up by journald under systemd) or `syslog` (calls `syslog(3)` with `LOG_DAEMON`; for OpenWRT / non-systemd hosts).

Runtime behaviour that doesn't affect which code is linked lives in `${ZWAVED_CONFIG:-/etc/zwaved/zwaved.conf}`, parsed by `src/config/Config.cpp`. The format is a minimal INI-flavoured key/value file (sections in `[brackets]`, `key = value`, `#` comments, repeated keys allowed for `dongles.accept`) and the parser is hand-rolled — no third-party dependency. Sections wired today: `[logger] min_level`, `[storage] state_dir`, `[dongles] accept = vid:pid:name` (one row per accepted dongle, replaces the hardcoded Aeotec VID/PID), `[behavior] auto_lifeline`. **The Config module doesn't expose a public accessor** — it parses the file at constructor priority 102 and **publishes four retained events on `MessageBus`** (`LoggerConfig`, `StorageConfig`, `DonglesConfig`, `BehaviorConfig`). Each consuming module subscribes from its own constructor and picks up the cached value via replay-on-subscribe. A fully-commented sample sits at `etc/zwaved.conf`. There is no SIGHUP reload — restart the daemon to pick up changes.

## Code Quality

Both tools must pass before commits are accepted (enforced by the pre-commit hook in `scripts/check-format`).

```bash
# Check formatting (no changes)
clang-format --dry-run --Werror src/**/*.cpp src/**/*.hpp src/*.cpp src/*.hpp

# Apply formatting
clang-format -i src/**/*.cpp src/**/*.hpp src/*.cpp src/*.hpp

# Run static analysis (uses compile_commands.json from the configured build dir)
cmake --build cmake-build-gnu --target check

# Auto-fix static analysis issues
cmake --build cmake-build-gnu --target fix-tidy
```

The `check` and `fix-tidy` targets read `compile_commands.json` from the build directory, so the preset must have been configured at least once. `CMAKE_EXPORT_COMPILE_COMMANDS=ON` is set in every preset.

### Pre-commit hook setup

```bash
chmod +x scripts/check-format
ln -sfn ../../scripts/check-format .git/hooks/pre-commit
```

The hook checks only staged C/C++ files with `clang-format` and `clang-tidy`. All `clang-tidy` warnings are treated as errors (`WarningsAsErrors: '*'`). The same script can be run manually (`scripts/check-format`) or with `--fix` to apply `clang-format -i` to staged files and re-stage them. The script detects git-hook context via the `GIT_INDEX_FILE` env var (which git sets for any hook that touches the index) and ignores `--fix` in that context — auto-mutating the index mid-commit is surprising even when harmless.

## Architecture

The application is a Z-Wave communication daemon built around GCC/Clang's `__attribute__((constructor(N)))`/`__attribute__((destructor(N)))` mechanism — threads start **before** `main()` and stop **after** it returns. Priority constants are defined in `src/zwaved.h`:

| Priority | Component | File |
|----------|-----------|------|
| 101 | Logger — MPSC queue + dedicated `ZWaveLog` consumer thread; producers never block on I/O. Sink (stdout / syslog) picked at build time via `ZWAVED_LOGGER_SINK`. Comes up first so every later constructor / destructor can call `Logger::info(...)` | `src/logger/Logger.cpp` |
| 102 | Config — parses `${ZWAVED_CONFIG:-/etc/zwaved/zwaved.conf}` and publishes four retained events on `MessageBus` (`LoggerConfig`, `StorageConfig`, `DonglesConfig`, `BehaviorConfig`). Modules subscribe from their own constructors; the bus is the contract | `src/config/Config.cpp` |
| 103 | SignalHandler — registers SIGHUP/SIGTERM/SIGINT, sets `applicationRunning = true`. Logs registration via Logger; runs after Config so Logger's `min_level` threshold is already in effect. The actual signal handlers stay on `std::cout` because Logger takes a mutex which is not async-signal-safe | `src/SignalHandler.cpp` |
| 201 | ZWave dongle monitor thread — uses `libudev` to detect USB insertion/removal and publishes the TTY path on `MessageBus`. Subscribes to `DonglesConfig` to learn the accepted-dongle VID/PID list | `src/zwave-dongle/MonitorThread.cpp` |
| 202 | ZWave protocol thread — opens the serial port, runs the host-API frame transport, dispatches Add/Remove/SendData requests. Subscribes to `BehaviorConfig` for the auto-lifeline toggle | `src/zwave-protocol/ProtocolThread.cpp` |
| 203 | External API thread — runs the configured transport backend (D-Bus via sdbus-c++ today; ubus reserved) | `src/external-api/ExternalApiThread.cpp` |

`main()` in `src/main.cpp` only loops on `applicationRunning` (declared in `SignalHandler.hpp`); all lifecycle is handled by constructor/destructor attributes.

**Shutdown teardown:** GCC places `__attribute__((destructor))` functions in the ELF `.fini_array` section, which the dynamic linker runs *after* the C runtime has already torn down `__cxa_atexit`-registered statics. Each module's static singleton (`state()` returning a function-local static) therefore has its `std::thread` member destroyed *before* the matching `stopXxx()` destructor function would have joined it — `std::thread::~thread()` on a still-joinable handle calls `std::terminate`. Each module's state struct accordingly carries an explicit destructor that does the running=false / notify / join itself, and the priority-coupled construction order means the four state destructors fire in the same priority order an `__attribute__((destructor))` chain would have given. `MessageBus::touch()` is called from the Logger's constructor (priority 101) so the bus's static state is registered with `__cxa_atexit` *before* any module's, guaranteeing the bus outlives every joining-thread destructor that may call `unsubscribe(...)` on its way out.

Each component owns its thread and `running` flag inside an anonymous namespace, names its thread via `prctl(PR_SET_NAME, ...)` (`ZWaveComm`, `ZWaveProto`, `ZWaveExtApi`), and joins it from the destructor. Inter-thread channels under `src/zwave-protocol/` (`DeviceHandoff`, `HostApiRequestQueue`, `HostApiCallbackDispatcher`) all use `std::mutex` + `std::condition_variable` so blocking waits unblock cleanly on shutdown. The dongle monitor (VID `0658`, PID `0200` — Aeotec Z-Stick Gen5) uses `select()` with a 1-second timeout to watch udev events while remaining stoppable.

`src/zwave-protocol/` contains:
- Z-Wave serial framing classes (`ZwaveDataFrame`, `ZwaveACCFrame`, `ZwaveNAKFrame`, `ZwaveCANFrame`) implementing the serial API frame format
- `SerialPort` (RAII 115200 8N1 raw TTY wrapper) and `FrameTransport` (six-frame-flow state machine with retry-on-NAK/CAN/timeout backoff `Tn = 100 + n × 1000 ms`)
- `HostApi` codec for FUNC_ID_ZW_ADD_NODE_TO_NETWORK (`0x4A`, modes `0x01`/`0x05`/`0x06`/`0x08`/`0x09`), FUNC_ID_ZW_REMOVE_NODE_FROM_NETWORK (`0x4B`, modes `0x01`/`0x05`), FUNC_ID_ZW_REMOVE_FAILED_NODE_ID (`0x61`) for evicting unresponsive nodes from the routing table (decodes both the synchronous response and the async callback frame), FUNC_ID_ZW_SEND_DATA (`0x13`) for application-layer commands addressed to a specific node, plus FUNC_ID_GET_VERSION (`0x15`), FUNC_ID_MEMORY_GET_ID (`0x20`) and FUNC_ID_SERIAL_API_GET_INIT_DATA (`0x02`) for dongle introspection on connect (the last seeds NodeRegistry from the dongle's node bitmap so the network state survives daemon restarts even before any inclusion is observed)
- Application-layer command-class codecs live under `src/zwave-protocol/application/`. Each one produces and parses the byte sequence that goes inside FUNC_ID_ZW_SEND_DATA payloads; they don't touch the dongle directly. Currently:
  - `BinarySwitch` for Command Class `0x25` (Set / Get / Report); pairs with `SendDataRequest` to drive On/Off at a node
  - `Association` for Command Class `0x85` (Set / Get / Report / Remove / Groupings Get / Groupings Report); pairs with `SendDataRequest` to manage per-group association lists
  - New CCs (Multilevel Switch, Configuration, etc.) belong here and just need adding to `src/zwave-protocol/application/CMakeLists.txt`
- `HostApiSession` tracks the single active inclusion/exclusion session and correlates requests with their callbacks
- Inter-thread channels: `HostApiRequestQueue` (in: `AddNodeRequest` / `RemoveNodeRequest` / `SendDataRequest` variant) and `HostApiCallbackDispatcher` (out: `NodeStatusCallback` / `SendDataCallback` variant)

`src/external-api/` exposes the host API to clients. The transport-agnostic interface is `ExternalApi::IBackend` declared in `IExternalApi.hpp`, plus a `createBackend()` factory; `DBusBackend` is the only implementation today (system bus, name `com.tiunda.ZWaved`, object `/com/tiunda/ZWaved`, interface `com.tiunda.ZWaved1`). The system-bus policy XML is at `dbus/com.tiunda.ZWaved.conf` and must be installed under `/etc/dbus-1/system.d/` for non-root callers. Operator usage (classic vs SmartStart inclusion, signal payload layout, status table, troubleshooting) is in `MANUAL.md`.

`src/node-registry/NodeRegistry` is an in-memory registry of currently-included Z-Wave nodes (nodeId + device-class triple + supported command classes), backed by **SQLite** so it survives daemon restarts. Populated by the protocol thread when an inclusion completes (status `0x06`) and trimmed when an exclusion completes; queried by `DBusBackend::GetNodes`. Static info only — dynamic per-node state (e.g. last Binary Switch on/off) flows through CC-specific D-Bus signals rather than being duplicated here. **Rows are keyed by `(home_id, node_id)`**, so swapping in a different dongle (different home ID) loads a fresh in-memory cache for that network without disturbing the previous network's stored nodes — `ProtocolThread` calls `NodeRegistry::setHomeId(homeIdBytes)` after introspection to bind the cache. The database file lives at `${ZWAVED_STATE_DIR:-/var/lib/zwaved}/nodes.db`; the directory is created on first use. Schema migrations are tracked via `PRAGMA user_version`. Persistence is best-effort: if the path can't be opened, the daemon logs a warning and falls back to in-memory only. Resolved at link time via `pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)` (system `libsqlite3-dev`, ubiquitous on Ubuntu / Yocto / OpenWRT).

`src/message-bus/MessageBus` is the in-process publish/subscribe bus and the **only** coupling between the dongle monitor and the protocol thread: the monitor publishes `DongleStatus` events (connected with TTY path / disconnected) and the protocol thread subscribes to them, blocking on its own condition variable until a path arrives. The bus also carries `ApplicationCommand` events — unsolicited Command Class frames received from nodes via `FUNC_ID_APPLICATION_COMMAND_HANDLER` (0x04), published by the protocol thread and consumed by the D-Bus backend (which re-emits them as raw + typed D-Bus signals). State events (`DongleStatus`) are retained — a late subscriber sees the latest value on subscribe. Transient events (`ApplicationCommand`) are not retained. Implementation is a thin wrapper around the header-only **eventpp** library (resolved via `find_package(eventpp REQUIRED)` — expected from system / cross-sysroot, not vendored); eventpp does not appear in the public header so the backing library is swappable. Dispatch is synchronous under the bus mutex; handlers must not call `publish()` or `subscribe()` reentrantly.

`src/logger/Logger` is the daemon's async logger. Producers call `Logger::info(...)` / `Logger::warn(...)` / `Logger::error(...)` / `Logger::debug(...)` — these push a (level, timestamp, message) entry onto an MPSC queue protected by `std::mutex` + `std::condition_variable` and return immediately. A dedicated consumer thread (`ZWaveLog`, constructor priority 101) drains the queue in batches and writes to the configured sink. Sink selection is build-time only via the `ZWAVED_LOGGER_SINK` CMake cache option: `stdout` (default — captured by journald under systemd, ends up under `/var/log/journal/`) or `syslog` (calls `syslog(3)` with `LOG_DAEMON` facility — appropriate for OpenWRT / non-systemd hosts where logd or rsyslog routes to `/var/log/messages`). The Logger is intentionally **not** routed through `MessageBus`: bus dispatch is synchronous on the publisher's thread, which would defeat the whole point of having a separate logger thread. Migration of existing `std::cout` / `std::cerr` call sites to `Logger::info` / `Logger::error` is incremental; the two coexist without conflict.

Under the syslog sink the Logger constructor additionally calls `Logger::claimStandardStreams()`, which reopens stdin on `/dev/null` and dup2s pipe-write-ends over `STDOUT_FILENO` / `STDERR_FILENO`. Two reader threads (`ZWaveCapO`, `ZWaveCapE`) drain those pipes line-by-line into `Logger::info` / `Logger::error`, so every legacy `std::cout` / `std::cerr` / `printf` / library panic also lands in syslog without requiring source migration. The captures are torn down in the destructor *before* the consumer thread, so trailing partial lines are flushed while the queue is still alive. Under the stdout sink `claimStandardStreams()` is a deliberate no-op: capturing stdout while Logger writes to it would loop the consumer's own output back through its own pipe.

`utils/` holds optional companion binaries that talk to the daemon over its external API; built only when `ZWAVED_BUILD_UTILS=ON` (default). Today this is just `utils/zwave-terminal/` — an ncurses TUI client that connects to `com.tiunda.ZWaved` over the system bus to drive Add/Remove Node operations and watch the resulting signal traffic. It uses `pkg-config` for `ncurses` and `sdbus-c++` (re-resolving the latter only if the daemon build hasn't already done so).

## Naming Conventions (enforced by clang-tidy)

- Classes: `UpperCamelCase`
- Functions, variables, parameters: `camelBack`
- Global constants: `UPPER_CASE`