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

Requires: GCC 15 (`g++-15`), LLVM/Clang 20 (`clang++-20`), CMake 3.20+, `libudev-dev`. C++26 standard.

## Code Quality

Both tools must pass before commits are accepted (enforced by the pre-commit hook in `gitscripts/pre-commit`).

```bash
# Check formatting (no changes)
clang-format --dry-run --Werror src/**/*.cpp src/**/*.hpp src/*.cpp src/*.hpp

# Apply formatting
clang-format -i src/**/*.cpp src/**/*.hpp src/*.cpp src/*.hpp

# Run static analysis (uses compile_commands.json from build dir)
cmake --build cmake-build-gnu --target check

# Auto-fix static analysis issues
cmake --build cmake-build-gnu --target fix-tidy
```

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
| 201 | ZWave dongle monitor thread — uses `libudev` to detect USB insertion/removal | `src/zwave-dongle/MonitorThread.cpp` |
| 202 | ZWave protocol thread — placeholder communication loop | `src/zwave-protocol/ProtocolThread.cpp` |

`main()` in `src/main.cpp` only loops on `applicationRunning` (declared in `SignalHandler.hpp`); all lifecycle is handled by constructor/destructor attributes.

Each component owns its thread and `running` flag inside an anonymous namespace. The dongle monitor (VID `0658`, PID `0200`) uses `select()` with a 1-second timeout to watch udev events while remaining stoppable.

`src/zwave-protocol/` also contains Z-Wave serial framing classes (`ZwaveDataFrame`, `ZwaveACCFrame`, `ZwaveNAKFrame`, `ZwaveCANFrame`) implementing the Z-Wave serial API frame format.

## Naming Conventions (enforced by clang-tidy)

- Classes: `UpperCamelCase`
- Functions, variables, parameters: `camelBack`
- Global constants: `UPPER_CASE`