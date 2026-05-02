# zwaved - Z-Wave Communication Daemon

A C++ application that manages Z-Wave device communication through a dedicated thread. The application uses constructor and destructor attributes to automatically manage thread lifecycle.

## Features

- **Priority-managed component threads**: Each component starts before `main()` and stops after via `__attribute__((constructor(N)))` / `__attribute__((destructor(N)))` at known priorities (101 signals, 201 dongle monitor, 202 protocol, 203 external API)
- **USB hot-plug detection**: `libudev` watches for the Aeotec Z-Stick Gen5 (VID `0658`, PID `0200`) and publishes the discovered TTY path through a `DeviceHandoff` channel
- **Z-Wave Host API frame transport**: SOF / ACK / NAK / CAN parser, send-with-ACK retry on NAK/CAN/timeout (`Tn = 100 + n × 1000 ms`, 3 attempts), spec-compliant XOR checksum
- **Add Node / Remove Node support**: Classic inclusion (Mode `0x01`), SmartStart Listen (Mode `0x09`) and SmartStart Include (Mode `0x08`) for `0x4A`; classic exclusion for `0x4B`; stop via Mode `0x05`. Failed nodes can be evicted from the routing table via `0x61` (`FUNC_ID_ZW_REMOVE_FAILED_NODE_ID`).
- **External D-Bus interface (sdbus-c++)**: System bus name `com.tiunda.ZWaved`, methods `AddNode` / `StopAddNode` / `RemoveNode` / `StopRemoveNode` / `RemoveFailedNode` / `SetSwitchBinary` / `GetSwitchBinary` / `SetBasic` / `GetBasic` / `GetNodes` / `GetDongleInfo` / `GetInitData` / `SetAssociation` / `RemoveAssociation` / `GetAssociation` / `GetAssociationGroupings` / `SetMultichannelAssociation` / `RemoveMultichannelAssociation` / `GetMultichannelAssociation` / `GetMultichannelAssociationGroupings`, signals `NodeInclusionStatus` / `NodeExclusionStatus` / `DongleStatus` / `DongleInfo` / `InitData` / `SendDataStatus` / `ApplicationCommand` / `SwitchBinaryReport` / `AssociationReport` / `AssociationGroupingsReport` / `RemoveFailedNodeStatus`. See [MANUAL.md](MANUAL.md) for operator usage.
- **Pluggable transport backends**: A clean `IBackend` interface allows a future ubus backend to plug in without disturbing the protocol layer; selectable via the `ZWAVED_EXTERNAL_API` CMake cache option (`dbus` default; `ubus` and `both` reserved)
- **Multi-Compiler Support**: Build with GCC 15 or LLVM/Clang 20 using CMake presets, with optional `clang-tidy`-integrated variants

## Prerequisites

- **CMake** 3.20 or higher
- **GCC 15.2.0** or **LLVM/Clang 20.1.8** (or both for multi-compiler support)
- **POSIX-compliant system** (Linux)
- **C++26 standard support**
- **libudev** development files (`libudev-dev`) — USB device monitoring
- **sdbus-c++** 2.x — D-Bus interface; install from source (the Debian/Ubuntu package is still on 1.x). Requires `libsystemd-dev` for the underlying sd-bus
- **SQLite3** development files (`libsqlite3-dev`) — node-registry persistence
- **eventpp** (header-only) — in-process publish/subscribe bus; not packaged on most distros, install from source
- **GoogleTest** development files (`libgtest-dev`) — only needed when `ZWAVED_BUILD_TESTS=ON` (default)

See the [Dependencies](#dependencies) section below for what each
library does, where it's used, and how CMake discovers it.

## Installation

### Install Dependencies

#### Ubuntu/Debian

```bash
# GCC 15
sudo add-apt-repository ppa:ubuntu-toolchain-r/test -y
sudo apt update
sudo apt install -y g++-15 gcc-15

# LLVM 20
sudo apt install -y clang-20 llvm-20

# CMake + runtime libs
sudo apt install -y cmake libudev-dev libsqlite3-dev libsystemd-dev libgtest-dev

# sdbus-c++. The Ubuntu/Debian package `libsdbus-c++-dev` ships an older
# 1.x release; the daemon is built and tested against 2.x. Install from
# source. Requires libsystemd-dev (already pulled in above) for sd-bus.
git clone --depth 1 https://github.com/Kistler-Group/sdbus-cpp.git
cmake -S sdbus-cpp -B sdbus-cpp/build -DSDBUSCPP_BUILD_CODEGEN=OFF -DSDBUSCPP_BUILD_TESTS=OFF
cmake --build sdbus-cpp/build
sudo cmake --install sdbus-cpp/build
sudo ldconfig

# eventpp is header-only and not packaged on most distros; install from source.
git clone --depth 1 https://github.com/wqking/eventpp.git
cmake -S eventpp -B eventpp/build
sudo cmake --install eventpp/build
```

## Dependencies

Four third-party libraries are linked or included by zwaved. The CMake
build resolves each one out-of-tree — no copies are vendored — so the
host system (or, for cross builds, the sysroot) must provide them.

### `libudev` — USB hot-plug detection

Tracks the Aeotec Z-Stick Gen5 (VID `0658`, PID `0200`) coming and
going on the USB bus. `MonitorThread` opens a udev monitor socket and
`select()`s on it with a 1-second timeout so the watch loop stays
stoppable; on a `device-add` / `device-remove` event it publishes a
`MessageBus::DongleStatus` carrying the discovered TTY path (or empty
on disconnect). The protocol thread blocks on this status until a path
arrives.

- **Package (Debian/Ubuntu):** `libudev-dev`
- **Cross-builds:** part of `systemd` / `eudev`; available in Yocto
  meta-oe and OpenWRT
- **CMake discovery:** `pkg_check_modules(LIBUDEV REQUIRED IMPORTED_TARGET libudev)` →
  link target `PkgConfig::LIBUDEV`
- **Used by:** `src/zwave-dongle/MonitorThread.cpp`

### `sdbus-c++` — D-Bus interface

Implements the external API surface. The daemon owns the system bus
name `com.tiunda.ZWaved`, exports an object at `/com/tiunda/ZWaved`,
and exposes ~13 methods + ~11 signals on the
`com.tiunda.ZWaved1` interface (see [MANUAL.md](MANUAL.md)).
sdbus-c++ pulls in `libsystemd` transitively for the underlying
sd-bus bindings.

- **Package (Debian/Ubuntu):** the distro `libsdbus-c++-dev` ships an
  older 1.x release; zwaved is built and tested against 2.x, so it
  must be installed from source on Debian/Ubuntu (see install steps
  above). `libsystemd-dev` is still needed for the underlying sd-bus
  and is the only piece left to apt
- **Cross-builds:** Yocto meta-oe `sdbus-c++` recipe (currently 2.x);
  pulls in `systemd` (or `elogind` on systems without systemd)
- **Upstream:** [https://github.com/Kistler-Group/sdbus-cpp](https://github.com/Kistler-Group/sdbus-cpp)
- **CMake discovery:** `pkg_check_modules(SDBUS_CPP REQUIRED IMPORTED_TARGET sdbus-c++)` —
  only resolved when `ZWAVED_EXTERNAL_API` includes `dbus`
- **Used by:** `src/external-api/DBusBackend.{hpp,cpp}` and the
  `utils/zwave-terminal/` companion client
- **Runtime policy:** the system-bus policy XML at
  `dbus/com.tiunda.ZWaved.conf` must be installed under
  `/etc/dbus-1/system.d/` so non-root callers can reach the service
  (see [MANUAL.md](MANUAL.md) §1)

### `sqlite3` — node-registry persistence

Backs `NodeRegistry` so the included-node list survives daemon
restarts. Rows are keyed by `(home_id, node_id)`, so the same database
can hold rows for multiple Z-Wave networks; switching dongles loads a
fresh in-memory cache for the new network without disturbing the
previous one's stored rows. The database file lives at
`${ZWAVED_STATE_DIR:-/var/lib/zwaved}/nodes.db` and the directory is
created on first use. Schema version is tracked via
`PRAGMA user_version`; persistence is best-effort — if the path can't
be opened, the daemon logs a warning and falls back to in-memory only.

- **Package (Debian/Ubuntu):** `libsqlite3-dev`
- **Cross-builds:** ubiquitous; available out-of-the-box on Yocto and
  OpenWRT
- **CMake discovery:** `pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)` →
  link target `PkgConfig::SQLITE3`
- **Used by:** `src/node-registry/NodeRegistry.cpp`

### `eventpp` — in-process publish/subscribe bus

Header-only library backing `MessageBus` — the single seam between the
dongle monitor, protocol thread, registry, and external-API backends.
zwaved wraps `eventpp::CallbackList` behind a small templated facade
(`subscribe<T>` / `publish<T>` per event type), so the eventpp header
does not appear in any public include and the backing library is
swappable without touching call sites.

- **Package:** not packaged on most distros; `find_package(eventpp REQUIRED)`
  expects it from the system or cross-sysroot. Install from source
  (see Dependencies install step above) or via a Yocto meta-oe
  recipe.
- **Upstream:** [https://github.com/wqking/eventpp](https://github.com/wqking/eventpp)
  (header-only, MIT)
- **CMake discovery:** `find_package(eventpp REQUIRED)` — header-only,
  no link target needed (the headers are pulled in transitively where
  the wrapper consumes them)
- **Used by:** `src/message-bus/MessageBus.cpp`

## Compilation

### Using CMake Presets (Recommended)

#### With GCC 15

```bash
cd /home/martin/work/zwaved
cmake --preset gnu
cmake --build cmake-build-gnu
```

#### With LLVM/Clang 20

```bash
cd /home/martin/work/zwaved
cmake --preset llvm
cmake --build cmake-build-llvm
```

#### With clang-tidy integrated (analysis during build)

Two additional presets run clang-tidy automatically on every source file as part of compilation. Tidy errors fail the build, just like compiler errors. Only changed files are re-checked on incremental builds.

```bash
# GCC 15 + clang-tidy
cmake --preset gnu-tidy
cmake --build cmake-build-gnu-tidy

# LLVM/Clang 20 + clang-tidy
cmake --preset llvm-tidy
cmake --build cmake-build-llvm-tidy
```

### Manual Configuration

#### With GCC 15

```bash
cd /home/martin/work/zwaved
rm -rf cmake-build-debug
mkdir cmake-build-debug
cd cmake-build-debug
cmake -DCMAKE_TOOLCHAIN_FILE=../.cmake/gnu-toolchain.cmake -S .. -B .
make
```

#### With LLVM/Clang 20

```bash
cd /home/martin/work/zwaved
rm -rf cmake-build-debug
mkdir cmake-build-debug
cd cmake-build-debug
cmake -DCMAKE_TOOLCHAIN_FILE=../.cmake/llvm-toolchain.cmake -S .. -B .
make
```

## Running

### With GCC 15 (gnu preset)

```bash
./cmake-build-gnu/zwaved
```

### With LLVM/Clang 20 (llvm preset)

```bash
./cmake-build-llvm/zwaved
```

### Running Output

The daemon logs lifecycle events to stdout. Plug in the Aeotec Z-Stick and
you should see something like:

```
[SignalHandler] Registered SIGHUP/SIGTERM/SIGINT handlers
Z-Wave dongle inserted: ...
Z-Wave dongle tty node: /dev/ttyACM0
[SerialPort] opened /dev/ttyACM0 at 115200 8N1
[DBusBackend] listening on system bus as com.tiunda.ZWaved
```

External callers then drive Add/Remove Node operations via D-Bus — see
[MANUAL.md](MANUAL.md).

### Stop the Application

Press `Ctrl+C` (SIGINT) or send SIGTERM. Each component prints a
shutdown line as it joins.

## External API (D-Bus)

zwaved exposes the Z-Wave Host API on the **system bus**:

- **Bus name:** `com.tiunda.ZWaved`
- **Object:** `/com/tiunda/ZWaved`
- **Interface:** `com.tiunda.ZWaved1`
- **Methods:** `AddNode(y y y ay ay)`, `StopAddNode(y)`, `RemoveNode(y y y)`, `StopRemoveNode(y)`, `RemoveFailedNode(y y)`, `SetSwitchBinary(y b y)`, `GetSwitchBinary(y y)`, `SetBasic(y y y)`, `GetBasic(y y)`, `GetNodes() → a(yyyyay)`, `GetDongleInfo() → (s y ay y)`, `GetInitData() → (y y ay y y)`, `SetAssociation(y y ay y)`, `RemoveAssociation(y y ay y)`, `GetAssociation(y y y)`, `GetAssociationGroupings(y y)`, `SetMultichannelAssociation(y y ay a(yy) y)`, `RemoveMultichannelAssociation(y y ay a(yy) y)`, `GetMultichannelAssociation(y y y)`, `GetMultichannelAssociationGroupings(y y)`, `GetVersion() → (s s)`, `GetNetworkStatus() → (bssyubyyt)`
- **Signals:** `NodeInclusionStatus(y y q y y y ay)`, `NodeExclusionStatus(y y q y y y ay)`, `DongleStatus(b s)`, `DongleInfo(s y ay y)`, `InitData(y y ay y y)`, `SendDataStatus(y y)`, `ApplicationCommand(y y ay)`, `SwitchBinaryReport(y y)`, `AssociationReport(y y y y ay)`, `AssociationGroupingsReport(y y)`, `RemoveFailedNodeStatus(y y y y)`

Install the system bus policy once per host:

```bash
sudo install -m 0644 dbus/com.tiunda.ZWaved.conf /etc/dbus-1/system.d/
sudo systemctl reload dbus
```

The `ZWAVED_EXTERNAL_API` CMake cache option selects which transports
build into the binary — `dbus` (default), `ubus` (placeholder, not yet
implemented), or `both`. A future ubus backend will plug into the same
`IBackend` interface and mirror these method/signal names.

## Configuration

The daemon reads a minimal INI-flavoured config file at startup —
parsed by hand inside `src/config/Config.cpp`, no third-party parser
involved. Resolution order:

1. `$ZWAVED_CONFIG` env var, if set.
2. `/etc/zwaved/zwaved.conf`.
3. Built-in defaults (a missing file is **not** an error — the daemon
   logs `Config: no config file at … — publishing defaults` and
   continues).

The Config module doesn't expose a public accessor. It parses the
file once and **publishes four retained events on `MessageBus`**:
`LoggerConfig`, `StorageConfig`, `DonglesConfig`, `BehaviorConfig`.
Each consuming module subscribes from its own constructor and picks
up the cached value via the bus's replay-on-subscribe semantics —
the bus *is* the contract. There is no SIGHUP-style reload; restart
the daemon to pick up changes.

### Example

```ini
[logger]
min_level = info       ; debug | info | warn | error

[storage]
state_dir = /var/lib/zwaved

[dongles]
accept = 0658:0200:Aeotec Z-Stick Gen5
# accept = 0658:0280:Aeotec Z-Stick 7

[behavior]
auto_lifeline = true   ; auto-populate group 1 on Z-Wave Plus inclusion
```

A fully-commented sample lives at [`etc/zwaved.conf`](etc/zwaved.conf).
Install with:

```bash
sudo install -D -m 0644 etc/zwaved.conf /etc/zwaved/zwaved.conf
```

What stays in **CMake** (not config-file): the logger sink kind
(`ZWAVED_LOGGER_SINK`), which transports are linked
(`ZWAVED_EXTERNAL_API`), and whether the utils are built
(`ZWAVED_BUILD_UTILS`). Anything that affects which code is *linked*
is build-time; anything that affects observable behavior at *runtime*
moves to the config file.

## Versioning

The daemon carries its version through two values that show up in
the same places (CLI, log, D-Bus method) and that originate at
configure time:

- **`SEMVER`** — `0.1.0`, set in the root `CMakeLists.txt` via
  `project(zwaved VERSION 0.1.0 LANGUAGES CXX)`. Bumped manually
  when a release is cut.
- **`GIT_DESCRIBE`** — the result of `git describe --tags --dirty
  --always` at configure time. Looks like `v0.1.0` on a clean
  release tag, `v0.1.0-12-gabcdef0` 12 commits past v0.1.0, or
  `v0.1.0-12-gabcdef0-dirty` with uncommitted changes.

CMake fills both into `src/Version.hpp.in` and emits the resulting
`Version.hpp` under the build tree (`cmake-build-*/generated/`).

The version is exposed three ways:

```bash
# CLI flag — short-circuits before main() runs the loop:
./cmake-build-gnu/zwaved --version
# zwaved 0.1.0 (v0.1.0-12-gabcdef0-dirty)

# Logger startup line — written by main() before the wait loop:
# zwaved 0.1.0 (v0.1.0-12-gabcdef0-dirty) starting

# D-Bus method:
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved \
    com.tiunda.ZWaved1 GetVersion
# (ss) "0.1.0" "v0.1.0-12-gabcdef0-dirty"
```

The version is captured at *configure* time, not build time. Run
`cmake --preset gnu` (or just `cmake .` in the build dir) to refresh
the git-describe value after committing or tagging — incremental
builds keep the value pinned to whatever was current the last time
configure ran. (Future enhancement: a custom target with
`BYPRODUCTS` that re-evaluates `git describe` on every build.)

## Continuous Integration

`.github/workflows/build.yml` runs on every push and pull-request.
The `Dockerfile` is the single source of truth for "how zwaved is
built": CI runs `docker build` against the same file a developer
uses locally, which means a broken commit fails CI the same way it
fails a `docker build .` on your machine. The workflow stops at the
Dockerfile's `build` stage (the toolchain stage that runs
`ctest --output-on-failure`), so the artifact CI produces is "this
commit compiles and its tests pass", not a publishable image.

When a `v*.*.*` tag is pushed (e.g. `git tag v0.1.0 && git push --tags`),
an additional `publish-image` job kicks in. It builds the full
Dockerfile (including the slim runtime stage), tags the result as
`ghcr.io/<owner>/<repo>:0.1.0` and `…:latest`, and pushes to
**GitHub Packages** (ghcr.io). The image's `--version` output and
`org.opencontainers.image.version` label reflect the tag.

Pulling and running:

```bash
docker pull ghcr.io/<owner>/<repo>:0.1.0
docker run --rm ghcr.io/<owner>/<repo>:0.1.0 --version
# zwaved 0.1.0 (v0.1.0)
```

Caveat: a real production deployment normally prefers running the
binary directly under systemd on the host — the daemon needs access
to the host's D-Bus and `/dev/ttyACM*`, which is awkward through a
container without `--privileged`, `--device=/dev/ttyACM0`, and a
host D-Bus bind-mount. The published image is most useful as a
build-output artifact and a way to inspect a known-good build.

## Logging

zwaved runs an asynchronous logger on a dedicated `ZWaveLog` thread.
Producers call `Logger::info(...)` / `Logger::warn(...)` /
`Logger::error(...)` / `Logger::debug(...)` (declared in
`src/logger/Logger.hpp`); the calls push onto an MPSC queue and return
immediately, so a slow log sink never stalls the protocol or
external-API threads.

The sink is picked at build time via the `ZWAVED_LOGGER_SINK` CMake
cache option:

- `stdout` (default) — formatted lines `[ISO-8601 UTC] [LEVEL] message`
  go to stdout. Under systemd the journal captures them automatically
  (`journalctl -u zwaved`, persisted under `/var/log/journal/`).
- `syslog` — calls `syslog(3)` with the `LOG_DAEMON` facility and
  `zwaved` ident; severity maps from the logger level. Suitable for
  OpenWRT / non-systemd deployments where logd or rsyslog routes to
  `/var/log/messages`. Under this sink the daemon also reopens stdin
  on `/dev/null` and reroutes stdout / stderr through Logger via
  pipes, so any rogue `printf`, `std::cout`, `std::cerr`, library
  panic or assert trace ends up in syslog alongside the structured
  log output. (Capture is intentionally **not** enabled under the
  `stdout` sink — Logger's own writes would loop back through its
  own pipe.)

```bash
cmake --preset gnu -DZWAVED_LOGGER_SINK=syslog
cmake --build cmake-build-gnu
```

For full operator usage (classic vs SmartStart inclusion, signal
progression, flag-byte layout, status table, troubleshooting), see
[MANUAL.md](MANUAL.md).

## Project Structure

```
zwaved/
├── CMakeLists.txt                  # Root CMake configuration
├── CMakePresets.json               # CMake presets (gnu, llvm, gnu-tidy, llvm-tidy)
├── README.md                       # This file
├── MANUAL.md                       # D-Bus operator manual
├── CLAUDE.md                       # Notes for Claude Code
├── dbus/
│   └── com.tiunda.ZWaved.conf      # System bus policy file
├── docs/                           # Z-Wave Host API specification (PDF)
├── src/
│   ├── CMakeLists.txt
│   ├── main.cpp                    # Loops on applicationRunning until shutdown
│   ├── zwaved.h                    # Component priority constants
│   ├── SignalHandler.hpp/.cpp      # SIGHUP/SIGTERM/SIGINT handlers (priority 101)
│   ├── zwave-dongle/               # USB monitoring (priority 201)
│   │   ├── CMakeLists.txt
│   │   ├── MonitorThread.cpp       # libudev hot-plug detection
│   │   └── DeviceHandoff.hpp/.cpp  # Single-slot TTY path channel
│   ├── zwave-protocol/             # Serial I/O + host API (priority 202)
│   │   ├── CMakeLists.txt
│   │   ├── ProtocolThread.cpp      # Main protocol event loop
│   │   ├── SerialPort.hpp/.cpp     # RAII 115200 8N1 raw TTY wrapper
│   │   ├── FrameTransport.hpp/.cpp # SOF/ACK/NAK/CAN parser + retry
│   │   ├── HostApi.hpp/.cpp        # 0x4A/0x4B encoders + callback decoder
│   │   ├── HostApiSession.hpp/.cpp # Active session tracker
│   │   ├── HostApiRequestQueue.hpp/.cpp
│   │   ├── HostApiCallbackDispatcher.hpp/.cpp
│   │   ├── ZwaveDataFrame.hpp/.cpp # SOF data frame
│   │   ├── ZwaveACCFrame.hpp/.cpp  # ACK (0x06)
│   │   ├── ZwaveNAKFrame.hpp/.cpp  # NAK (0x15)
│   │   └── ZwaveCANFrame.hpp/.cpp  # CAN (0x18)
│   └── external-api/               # External transports (priority 203)
│       ├── CMakeLists.txt
│       ├── IExternalApi.hpp        # Backend interface
│       ├── ExternalApiThread.cpp   # Component lifecycle + factory
│       └── DBusBackend.hpp/.cpp    # sdbus-c++ implementation
├── .cmake/
│   ├── gnu-toolchain.cmake
│   └── llvm-toolchain.cmake
├── .clang-format
├── .clang-tidy
├── scripts/
│   └── pre-commit                  # clang-format + clang-tidy gate
├── .gitignore
└── cmake-build-*                   # Build directories (generated)
```

## Modular CMake Build System

The project uses a modular CMake structure for better organization and scalability:

- **Root CMakeLists.txt**: Defines the project, finds dependencies (`libudev`, `sdbus-c++`), and exposes `ZWAVED_EXTERNAL_API`
- **src/CMakeLists.txt**: Processes subdirectories for the dongle monitor, protocol, and external-API components
- **src/zwave-dongle/CMakeLists.txt**: USB hot-plug monitoring + device handoff sources
- **src/zwave-protocol/CMakeLists.txt**: Serial port, frame transport, host-API codec, and session/queue sources
- **src/external-api/CMakeLists.txt**: External-transport backends (gated on `ZWAVED_EXTERNAL_API`)

This hierarchical structure allows:
- Clean separation of concerns
- Easy addition of new components
- Maintainable and scalable build configuration
- Each component can have its own build rules and dependencies in the future

To add a new component:
1. Create a new subdirectory in `src/`
2. Create a `CMakeLists.txt` file in that directory
3. Update the parent `CMakeLists.txt` to include the new subdirectory

## Technical Details

### Thread Lifecycle

Each component is owned by a dedicated thread, started before `main()`
via `__attribute__((constructor(N)))` and stopped after `main()` returns
via `__attribute__((destructor(N)))`. Priorities are defined in
`src/zwaved.h`:

| Priority | Component | Thread name | Purpose |
|----------|-----------|-------------|---------|
| 101 | `SignalHandler` | (no thread) | Registers signal handlers; sets `applicationRunning = true` |
| 201 | `MonitorThread` | `ZWaveComm` | libudev hot-plug → `DeviceHandoff::publish` |
| 202 | `ProtocolThread` | `ZWaveProto` | Awaits TTY path → opens `SerialPort` → runs `FrameTransport` event loop |
| 203 | `ExternalApiThread` | `ZWaveExtApi` | Runs the configured backend (D-Bus today) |

Components communicate through three thread-safe channels in
`src/zwave-protocol/`: `DeviceHandoff` (monitor → protocol),
`HostApiRequestQueue` (external API → protocol), and
`HostApiCallbackDispatcher` (protocol → external API). All three use
`std::mutex` + `std::condition_variable` for blocking waits with
shutdown wake-up.


### Signal Handling

The application registers handlers for the following signals:

- **SIGHUP (Signal 1)**: Hangup/reload signal
  - Used for configuration reload
  - Prints notification and continues running
  
- **SIGTERM (Signal 15)**: Termination signal
  - Gracefully shuts down the application
  - Sets `shouldExit` flag to stop the main loop
  - All threads complete their cycles and exit cleanly
  
- **SIGINT (Signal 2)**: Interrupt signal (Ctrl+C)
  - Same behavior as SIGTERM
  - Provides graceful shutdown when user presses Ctrl+C

**Important Note**: SIGKILL (Signal 9) cannot be caught or blocked - it terminates the process immediately without cleanup. Use SIGTERM instead for graceful shutdown.

Send signals to the application:
```bash
# Graceful shutdown
kill -TERM <pid>

# Or with killall
killall -TERM zwaved

# Reload signal
kill -HUP <pid>

# Find the PID
ps aux | grep zwaved
```

- **Names**: `ZWaveComm`, `ZWaveProto`, `ZWaveExtApi` (set via `prctl(PR_SET_NAME, ...)`)
- **Type**: `std::thread` stored in component-local anonymous namespaces
- **Synchronization**: `std::atomic<bool>` for stop signaling; mutex + cv for the inter-thread channels
- **Port**: `/dev/ttyACMx` discovered at runtime by libudev; the protocol thread reopens automatically on hot-plug

### Available Compilers

| Compiler | Version | Support |
|----------|---------|---------|
| GCC | 15.2.0 | Full (constructor/destructor attributes supported) |
| Clang | 20.1.8 | Full (constructor/destructor attributes supported) |

**Note**: Modern C++ attribute syntax `[[gnu::constructor]]` and `[[gnu::destructor]]` are not yet supported in GCC or Clang. The code uses `__attribute__((constructor(101)))` and `__attribute__((destructor(101)))` instead for compatibility.

## Building with Different C++ Standards

To change the C++ standard, edit `CMakeLists.txt`:

```cmake
set(CMAKE_CXX_STANDARD 26)  # Change to 20, 17, etc.
```

## Troubleshooting

### Thread is not printing
- Ensure the application is running long enough (the loop in `main()` continues indefinitely)
- Check that `running` flag is being set correctly in the constructor

### Z-Wave device not detected
- Connect the Aeotec Z-Stick Gen5 USB controller
- Verify with: `lsusb | grep Sigma`
- Check device file: `ls /dev/ttyACM0`

### Build errors with compiler
- Verify compiler is installed: `g++-15 --version` or `clang++-20 --version`
- Clear CMake cache: `rm CMakeCache.txt` in the build directory
- Reconfigure: `cmake --preset gnu` or `cmake --preset llvm`

## Code Quality Tools

### Tests

Unit tests live under `tests/`, built when `ZWAVED_BUILD_TESTS=ON`
(default). The test executables link only the source files directly
under test — they don't drag in libudev / sdbus-c++ / sqlite3, so
they build in seconds and run in milliseconds.

```bash
cmake --build cmake-build-gnu                       # builds tests too
ctest --test-dir cmake-build-gnu --output-on-failure
```

Each test executable also runs standalone:

```bash
./cmake-build-gnu/tests/HostApi_test --gtest_filter='HostApi.EncodeAddNode*'
```

GoogleTest discovery (`gtest_discover_tests`) registers every
`TEST()` with CTest at build time, so adding a new `TEST(...)` shows
up in the suite without touching CMake.

To skip the tests entirely (e.g. on a build host without
`libgtest-dev`):

```bash
cmake --preset gnu -DZWAVED_BUILD_TESTS=OFF
```

### Editor / IDE setup

Two repo-level files keep editors aligned without per-IDE configuration:

- **`.editorconfig`** — universal indentation, line endings, trailing
  whitespace, final newline. Read by CLion, VSCode, Vim/Neovim,
  Emacs and most other modern editors. clang-format remains
  authoritative for C/C++ files; this file covers what clang-format
  doesn't touch (CMake, Markdown, JSON, shell, makefiles).
- **`.clang-format`** — the actual C/C++ formatter spec (4-space
  indent, 120-char lines, Allman braces, left-aligned pointers).

For git hooks, run `scripts/install-hooks` once after cloning — it
points `core.hooksPath` at `scripts/git-hooks/` so every clone runs
the same hooks regardless of which editor or workflow you use. See
the [Pre-commit Hook](#pre-commit-hook) subsection for details.

### Code Formatting with clang-format

The project uses `.clang-format` to maintain consistent code style following C++26 standards with:
- 4-space indentation
- 120-character line limit
- Allman brace style
- Pointer alignment to the left

#### Format All Source Files

```bash
cd /home/martin/work/zwaved
clang-format -i *.cpp
```

#### Check Formatting Without Modifying

```bash
clang-format --dry-run -Werror *.cpp
```

#### Format Specific File

```bash
clang-format -i main.cpp ThreadManager.cpp
```

### Static Analysis with clang-tidy

The project uses `.clang-tidy` to perform static analysis and catch potential issues. The configuration includes:
- Bugprone checks
- C++ core guidelines
- Modernization suggestions
- Performance warnings
- Readability improvements
- Portability issues

#### Run Static Analysis

Ensure `clang-tidy` is installed:

```bash
sudo apt install -y clang-tools
```

Then run analysis on source files:

```bash
cd /home/martin/work/zwaved
clang-tidy main.cpp
clang-tidy ThreadManager.cpp
```

Or run with multiple threads:

```bash
clang-tidy main.cpp ThreadManager.cpp -j$(nproc)
```

#### Fix Issues Automatically

```bash
clang-tidy --fix main.cpp ThreadManager.cpp
```

### Automatic Code Quality Fixes

This project can automatically fix both formatting and static analysis issues with a single command.

#### Using CMake Targets (Recommended)

The easiest way to fix code quality issues is using CMake targets:

**Fix all issues (formatting + tidy):**
```bash
cd /home/martin/work/zwaved/cmake-build-gnu
cmake --build . --target fix
```

**Fix only formatting:**
```bash
cmake --build . --target fix-format
```

**Fix only static analysis issues:**
```bash
cmake --build . --target fix-tidy
```

**Check without fixing:**
```bash
cmake --build . --target check
```

These targets work with both build systems (also available for `cmake-build-llvm`).

Alternatively, use the `gnu-tidy` or `llvm-tidy` presets to run clang-tidy automatically on every file during the build, with incremental re-checking on subsequent builds (see [With clang-tidy integrated](#with-clang-tidy-integrated-analysis-during-build)).

#### Manual Command Line Approach

Alternatively, use the command-line tools directly:


```bash
cd /home/martin/work/zwaved

echo "Running clang-format..."
clang-format -i *.cpp

echo "Running clang-tidy..."
clang-tidy --fix *.cpp -- -I.

echo "Code quality fixes complete!"
```

Run the script:

```bash
chmod +x fix-code-quality.sh
./fix-code-quality.sh
```

#### Interactive Approach: Check Before Fixing

1. **Check formatting without modifying:**
   ```bash
   clang-format --dry-run -Werror *.cpp
   ```

2. **Check tidy issues without modifying:**
   ```bash
   clang-tidy *.cpp -- -I. 2>&1 | head -50
   ```

3. **Review the issues**, then apply fixes:
   ```bash
   clang-format -i *.cpp
   clang-tidy --fix *.cpp -- -I.
   ```

#### Fixing Specific Files

To fix only specific files:

```bash
# Format only ThreadManager.cpp
clang-format -i ThreadManager.cpp

# Tidy only main.cpp
clang-tidy --fix main.cpp -- -I.
```

#### Pre-commit Hook

The project includes a Git pre-commit hook script at:
```bash
scripts/check-format
```

The hook checks staged new and modified C/C++ files before commit using:

- `clang-format`
- `clang-tidy`

It blocks the commit if formatting or static analysis errors are found.

##### Enable the Pre-commit Hook

After cloning the repo, run the one-time setup script:

```bash
scripts/install-hooks
```

It points git's `core.hooksPath` at `scripts/git-hooks/`, where the
hook scripts (currently just `pre-commit`, a symlink to
`scripts/check-format`) live. From that point on every commit — from
the CLI, CLion, VSCode, or anything else that respects `git config`
— runs the same checks. The script is idempotent, and it cleans up
any stale `.git/hooks/pre-commit` symlink left over from the
pre-`core.hooksPath` workflow.

The big win is that **future hooks land in the repo, not in
`.git/hooks/`**. Add `commit-msg`, `pre-push`, etc. to
`scripts/git-hooks/` and every clone gets them after a fresh
`scripts/install-hooks` run.

##### Run the Hook Manually

You can run the same checks against currently-staged files at any time:

```bash
scripts/check-format
```

The script auto-detects whether it was invoked by `git commit` (via the
`GIT_INDEX_FILE` env var that git sets for hooks) and tailors its exit
message accordingly.

##### Fix Formatting Issues

If the hook reports formatting errors, the easiest fix is to re-run the
script with `--fix` — it applies `clang-format -i` to every staged
file and `git add`s the result back, so the formatting fixes go into
the same commit:

```bash
scripts/check-format --fix
```

`--fix` is intentionally a manual-only flag — when the script runs
inside the git pre-commit hook the flag is silently ignored, so
auto-mutating the index in the middle of a commit attempt never
surprises you. `clang-tidy` diagnostics are not auto-fixed; use
`cmake --build cmake-build-gnu --target fix-tidy` for those.

##### Fix clang-tidy Issues

If the hook reports clang-tidy errors, inspect the reported files and fix the issues manually, or run clang-tidy with automatic fixes where appropriate:
```bash clang-tidy --fix path/to/file.cpp ```

#### Suppressing Specific Issues

If certain clang-tidy warnings should not be auto-fixed, you can suppress them:

```cpp
// NOLINT(misc-include-cleaner)
prctl(PR_SET_NAME, "ZWaveComm", 0, 0, 0);
```

Or disable specific checks in `.clang-tidy`:

```yaml
Checks: '..., -google-runtime-int, -readability-magic-numbers'
```


## License

This project is provided as-is for educational and development purposes.

## CLion IDE Configuration

### Run and Debug Configurations

The project includes CLion run configurations stored in `.idea/runConfigurations/`:

- **zwaved (GNU GCC 15)**: Runs the application compiled with GCC 15 from `cmake-build-gnu`
- **zwaved (LLVM Clang 20)**: Runs the application compiled with LLVM/Clang 20 from `cmake-build-llvm`

### Using Run Configurations in CLion

1. Open the project in CLion
2. Select a run configuration from the dropdown menu in the top toolbar:
   - "zwaved (GNU GCC 15)" for GCC build
   - "zwaved (LLVM Clang 20)" for LLVM build
3. Click **Run** (Shift+F10) to execute the application
4. Click **Debug** (Shift+F9) to start debugging with breakpoints

### Setting Up CLion for the First Time

1. **Open Project**: File → Open → Select `/home/martin/work/zwaved`
2. **Configure CMake**: CLion will automatically detect `CMakeLists.txt`
3. **Select Preset**: 
   - Go to Settings → Tools → CMake → CMake options
   - Or use the CMake widget on the right panel
   - Choose `gnu` or `llvm` preset
4. **Build**: Press Ctrl+F9 or Build → Build Project

### Debugging with CLion

1. **Set Breakpoints**: Click on the line numbers in the source code to set breakpoints
2. **Start Debug Session**: Select configuration and click **Debug** (Shift+F9)
3. **Debug Actions**:
   - **Step Over** (F8): Execute next line
   - **Step Into** (F7): Enter function calls
   - **Step Out** (Shift+F8): Exit current function
   - **Continue** (F9): Resume execution
   - **Evaluate Expression** (Alt+F9): Inspect variables
4. **Inspect Thread**: 
   - Use the Debugger panel to view the "ZWaveComm" thread
   - Watch the `running` atomic flag value

### Viewing Thread Information

In CLion's Debugger panel:
- **Frames**: Shows the call stack for each thread
- **Variables**: Displays local and global variables
- **Breakpoints**: Lists all set breakpoints
- **Threads**: Shows all running threads including "ZWaveComm"

### Code Analysis in CLion

CLion automatically uses `.clang-tidy` and `.clang-format` configurations:

- **Code Inspections**: CLion highlights code issues based on clang-tidy rules
- **Reformat Code**: Code → Reformat Code (Ctrl+Alt+L) uses clang-format
- **Run Code Inspection**: Code → Run Inspection by Name to check specific rules

### Project Files Organization in CLion

The CLion configuration files are stored in `.idea/`:
- `cmake.xml` - CMake workspace configuration
- `misc.xml` - CLion project settings
- `workspace.xml` - IDEWorkspace state (auto-generated)
- `runConfigurations/zwaved_gnu.xml` - GNU run configuration
- `runConfigurations/zwaved_llvm.xml` - LLVM run configuration

