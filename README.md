# zwaved - Z-Wave Communication Daemon

A C++ application that manages Z-Wave device communication through a dedicated thread. The application uses constructor and destructor attributes to automatically manage thread lifecycle.

## Features

- **Priority-managed component threads**: Each component starts before `main()` and stops after via `__attribute__((constructor(N)))` / `__attribute__((destructor(N)))` at known priorities (101 signals, 201 dongle monitor, 202 protocol, 203 external API)
- **USB hot-plug detection**: `libudev` watches for the Aeotec Z-Stick Gen5 (VID `0658`, PID `0200`) and publishes the discovered TTY path through a `DeviceHandoff` channel
- **Z-Wave Host API frame transport**: SOF / ACK / NAK / CAN parser, send-with-ACK retry on NAK/CAN/timeout (`Tn = 100 + n × 1000 ms`, 3 attempts), spec-compliant XOR checksum
- **Add Node / Remove Node support**: Classic inclusion (Mode `0x01`), SmartStart Listen (Mode `0x09`) and SmartStart Include (Mode `0x08`) for `0x4A`; classic exclusion for `0x4B`; stop via Mode `0x05`
- **External D-Bus interface (sdbus-c++)**: System bus name `com.tiunda.ZWaved`, methods `AddNode` / `StopAddNode` / `RemoveNode` / `StopRemoveNode`, signals `NodeInclusionStatus` / `NodeExclusionStatus` / `DongleStatus`. See [MANUAL.md](MANUAL.md) for operator usage.
- **Pluggable transport backends**: A clean `IBackend` interface allows a future ubus backend to plug in without disturbing the protocol layer; selectable via the `ZWAVED_EXTERNAL_API` CMake cache option (`dbus` default; `ubus` and `both` reserved)
- **Multi-Compiler Support**: Build with GCC 15 or LLVM/Clang 20 using CMake presets, with optional `clang-tidy`-integrated variants

## Prerequisites

- **CMake** 3.20 or higher
- **GCC 15.2.0** or **LLVM/Clang 20.1.8** (or both for multi-compiler support)
- **POSIX-compliant system** (Linux)
- **C++26 standard support**
- **libudev** development files (`libudev-dev`) — USB device monitoring
- **sdbus-c++** development files (`libsdbus-c++-dev`) — D-Bus interface; pulls in `libsystemd-dev` transitively

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
sudo apt install -y cmake libudev-dev libsdbus-c++-dev
```

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
- **Methods:** `AddNode(y y y ay ay)`, `StopAddNode(y)`, `RemoveNode(y y y)`, `StopRemoveNode(y)`
- **Signals:** `NodeInclusionStatus(y y q y y y ay)`, `NodeExclusionStatus(y y q y y y ay)`, `DongleStatus(b s)`

Install the system bus policy once per host:

```bash
sudo install -m 0644 dbus/com.tiunda.ZWaved.conf /etc/dbus-1/system.d/
sudo systemctl reload dbus
```

The `ZWAVED_EXTERNAL_API` CMake cache option selects which transports
build into the binary — `dbus` (default), `ubus` (placeholder, not yet
implemented), or `both`. A future ubus backend will plug into the same
`IBackend` interface and mirror these method/signal names.

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
├── gitscripts/
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
```bash gitscripts/pre-commit``` 

The hook checks staged new and modified C/C++ files before commit using:

- `clang-format`
- `clang-tidy`

It blocks the commit if formatting or static analysis errors are found.

##### Enable the Pre-commit Hook

From the project root, make the script executable: 
```bash chmod +x gitscripts/pre-commit```

Then link it into Git's hooks directory: 
```bash ln -sf ../../gitscripts/pre-commit .git/hooks/pre-commit``` 

##### Run the Hook Manually

You can also run the hook manually before committing:
```bash ./gitscripts/pre-commit```

##### Fix Formatting Issues

If the hook reports formatting errors, run:
```bash clang-format -i src//*.cpp src//.hpp src/**/.h``` 

Then stage the fixed files again:
```bash git add <fixed-files>```

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

