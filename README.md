# zwaved - Z-Wave Communication Daemon

A C++ application that manages Z-Wave device communication through a dedicated thread. The application uses constructor and destructor attributes to automatically manage thread lifecycle.

## Features

- **Z-Wave Communication Thread**: A dedicated thread named "ZWaveComm" handles all Z-Wave device communication
- **Automatic Thread Management**: Thread starts before `main()` and stops after via `__attribute__((constructor))` and `__attribute__((destructor))`
- **Z-Wave Device Monitoring**: Monitors USB device insertion/removal events to detect Z-Wave dongle connection/disconnection
- **Multi-Compiler Support**: Build with GCC 15 or LLVM/Clang 20 using CMake presets
- **Thread Awareness**: The thread is aware of application start and stop events via atomic flags
- **Connected to Aeotec Z-Stick Gen5**: Supports communication with Z-Wave USB controllers via `/dev/ttyACM0`

## Prerequisites

- **CMake** 3.20 or higher
- **GCC 15.2.0** or **LLVM/Clang 20.1.8** (or both for multi-compiler support)
- **POSIX-compliant system** (Linux)
- **pthread** library
- **C++26 standard support**

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

# CMake
sudo apt install -y cmake
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

The application will output:
```
Z-Wave communication thread running
Hello and welcome to C++!
i = 1
i = 2
i = 3
i = 4
i = 5
Z-Wave communication thread running
Z-Wave communication thread running
...
```

The "Z-Wave communication thread running" message appears every second and will stop when the application exits.

### Stop the Application

Press `Ctrl+C` to gracefully stop the application. The thread will print:
```
Z-Wave communication thread stopping
```

## Project Structure

```
zwaved/
├── CMakeLists.txt                 # Root CMake configuration
├── CMakePresets.json              # CMake presets for GCC and LLVM
├── README.md                       # This file
├── src/
│   ├── CMakeLists.txt             # src subdirectory CMake configuration
│   ├── main.cpp                    # Main application entry point with signal handler registration
│   ├── zwave/
│   │   ├── CMakeLists.txt         # Z-Wave component CMake configuration
│   │   ├── ZWaveMonitor.cpp       # USB device monitoring implementation
│   │   ├── ZWaveMonitor.hpp       # USB device monitoring header
│   │   └── MonitorThread.cpp        # Z-Wave monitor thread startup/shutdown
│   └── zwave-protocol/
│       ├── CMakeLists.txt         # Protocol component CMake configuration
│       ├── ThreadManager.cpp       # Thread lifecycle management
│       ├── SignalHandler.cpp       # Signal handling (SIGHUP, SIGTERM, SIGINT)
│       └── SignalHandler.hpp       # Signal handler declarations
├── .cmake/
│   ├── gnu-toolchain.cmake        # GCC 15 toolchain configuration
│   └── llvm-toolchain.cmake       # LLVM/Clang 20 toolchain configuration
├── .clang-format                   # Code formatting rules
├── .clang-tidy                     # Static analysis rules
├── .gitignore                      # Git ignore patterns
└── cmake-build-*                  # Build directories (generated)
```

## Modular CMake Build System

The project uses a modular CMake structure for better organization and scalability:

- **Root CMakeLists.txt**: Defines the project, finds dependencies (libudev), and builds the main executable
- **src/CMakeLists.txt**: Processes subdirectories for Z-Wave and protocol components
- **src/zwave/CMakeLists.txt**: Z-Wave device monitoring component sources
- **src/zwave-protocol/CMakeLists.txt**: Thread management and protocol handling component sources

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

- **Start**: The `startZWaveThread()` function is executed before `main()` via `__attribute__((constructor(101)))`
- **Running**: The thread loops while `running` atomic flag is true, printing status every second
- **Stop**: The `stopZWaveThread()` function is executed after `main()` exits via `__attribute__((destructor(101)))`


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

- **Name**: "ZWaveComm" (set via `pthread_setname_np()`)
- **Type**: `std::thread` stored in global scope
- **Synchronization**: Uses `std::atomic<bool>` for thread-safe stop signaling
- **Port**: `/dev/ttyACM0` (when Z-Wave USB controller is connected)

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

To automatically fix code before committing (if using Git), create `.git/hooks/pre-commit`:

```bash
#!/bin/bash
# .git/hooks/pre-commit
clang-format -i *.cpp
clang-tidy --fix *.cpp -- -I.
git add *.cpp
exit 0
```

Make it executable:

```bash
chmod +x .git/hooks/pre-commit
```

#### Suppressing Specific Issues

If certain clang-tidy warnings should not be auto-fixed, you can suppress them:

```cpp
// NOLINT(cppcoreguidelines-pro-type-vararg)
pthread_setname_np(t.native_handle(), "ZWaveComm");
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

