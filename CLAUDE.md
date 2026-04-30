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

Requires: GCC 15 (`g++-15`), LLVM/Clang 20 (`clang++-20`), CMake 3.20+, `libudev-dev`, `libsdbus-c++-dev` (pulls in `libsystemd-dev`). C++26 standard.

The CMake cache option `ZWAVED_EXTERNAL_API` selects which external transports are built. Accepted values: `dbus` (default), `ubus` (placeholder, not implemented), `both`.

## Code Quality

Both tools must pass before commits are accepted (enforced by the pre-commit hook in `gitscripts/pre-commit`).

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
chmod +x gitscripts/pre-commit
ln -sf ../../gitscripts/pre-commit .git/hooks/pre-commit
```

The hook checks only staged C/C++ files with `clang-format` and `clang-tidy`. All `clang-tidy` warnings are treated as errors (`WarningsAsErrors: '*'`).

## Architecture

The application is a Z-Wave communication daemon built around GCC/Clang's `__attribute__((constructor(N)))`/`__attribute__((destructor(N)))` mechanism — threads start **before** `main()` and stop **after** it returns. Priority constants are defined in `src/zwaved.h`:

| Priority | Component | File |
|----------|-----------|------|
| 101 | SignalHandler — registers SIGHUP/SIGTERM/SIGINT, sets `applicationRunning = true` | `src/SignalHandler.cpp` |
| 201 | ZWave dongle monitor thread — uses `libudev` to detect USB insertion/removal and publishes the TTY path to `DeviceHandoff` | `src/zwave-dongle/MonitorThread.cpp` |
| 202 | ZWave protocol thread — opens the serial port, runs the host-API frame transport, dispatches Add/Remove Node requests | `src/zwave-protocol/ProtocolThread.cpp` |
| 203 | External API thread — runs the configured transport backend (D-Bus via sdbus-c++ today; ubus reserved) | `src/external-api/ExternalApiThread.cpp` |

`main()` in `src/main.cpp` only loops on `applicationRunning` (declared in `SignalHandler.hpp`); all lifecycle is handled by constructor/destructor attributes.

Each component owns its thread and `running` flag inside an anonymous namespace, names its thread via `prctl(PR_SET_NAME, ...)` (`ZWaveComm`, `ZWaveProto`, `ZWaveExtApi`), and joins it from the destructor. Inter-thread channels under `src/zwave-protocol/` (`DeviceHandoff`, `HostApiRequestQueue`, `HostApiCallbackDispatcher`) all use `std::mutex` + `std::condition_variable` so blocking waits unblock cleanly on shutdown. The dongle monitor (VID `0658`, PID `0200` — Aeotec Z-Stick Gen5) uses `select()` with a 1-second timeout to watch udev events while remaining stoppable.

`src/zwave-protocol/` contains:
- Z-Wave serial framing classes (`ZwaveDataFrame`, `ZwaveACCFrame`, `ZwaveNAKFrame`, `ZwaveCANFrame`) implementing the serial API frame format
- `SerialPort` (RAII 115200 8N1 raw TTY wrapper) and `FrameTransport` (six-frame-flow state machine with retry-on-NAK/CAN/timeout backoff `Tn = 100 + n × 1000 ms`)
- `HostApi` codec for FUNC_ID_ZW_ADD_NODE_TO_NETWORK (`0x4A`, modes `0x01`/`0x05`/`0x06`/`0x08`/`0x09`) and FUNC_ID_ZW_REMOVE_NODE_FROM_NETWORK (`0x4B`, modes `0x01`/`0x05`)
- `HostApiSession` tracks the single active inclusion/exclusion session and correlates requests with their callbacks
- Inter-thread channels: `HostApiRequestQueue` (in) and `HostApiCallbackDispatcher` (out)

`src/external-api/` exposes the host API to clients. The transport-agnostic interface is `ExternalApi::IBackend` declared in `IExternalApi.hpp`, plus a `createBackend()` factory; `DBusBackend` is the only implementation today (system bus, name `com.tiunda.ZWaved`, object `/com/tiunda/ZWaved`, interface `com.tiunda.ZWaved1`). The system-bus policy XML is at `dbus/com.tiunda.ZWaved.conf` and must be installed under `/etc/dbus-1/system.d/` for non-root callers. Operator usage (classic vs SmartStart inclusion, signal payload layout, status table, troubleshooting) is in `MANUAL.md`.

`src/zwave-dongle/DeviceHandoff` is the single-slot publication channel that hands the discovered TTY path from the monitor thread to the protocol thread; the protocol thread re-awaits on disconnect and reopens on reconnect.

`src/message-bus/MessageBus` is the in-process publish/subscribe bus for status broadcasts (e.g. `DongleStatus`) that fan out to one or more external-API backends. It is a thin wrapper around the header-only **eventpp** library (resolved via `find_package(eventpp REQUIRED)` — expected to be provided by the system / cross-sysroot, not vendored). Dispatch is synchronous on the publisher's thread; eventpp does not appear in the public header, so the underlying library can be replaced without touching call sites.

## Naming Conventions (enforced by clang-tidy)

- Classes: `UpperCamelCase`
- Functions, variables, parameters: `camelBack`
- Global constants: `UPPER_CASE`