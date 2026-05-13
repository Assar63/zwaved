# Onboarding

A short path from a fresh clone to your first compiling change. For
deep-dives, follow the links into [README.md](README.md),
[CLAUDE.md](CLAUDE.md), [MANUAL.md](MANUAL.md), and
[PreCommitHook.md](PreCommitHook.md) — this document only covers the
first hour.

## 1. Pick an environment

You have two supported options. Both build the exact same image CI
runs, so neither will surprise you later.

### Option A — devcontainer (recommended)

Requires Docker + VS Code with the *Dev Containers* extension (or
any other devcontainer-aware editor: JetBrains Gateway, GitHub
Codespaces, …).

1. Clone the repo and open it in VS Code.
2. Choose **Reopen in Container** when prompted.
3. Wait for the first build (a few minutes — third-party libs are
   compiled from source). Subsequent rebuilds hit the Docker layer
   cache.

The `postCreateCommand` runs `scripts/install-hooks` and `cmake
--preset gnu` for you, so the pre-commit hook is active and clangd
finds `compile_commands.json` before the editor finishes loading.

Hardware passthrough (real Z-Wave dongle) needs extra `runArgs` —
see the *Devcontainer* section in [README.md](README.md).

### Option B — native Linux

Install the toolchain and runtime deps from
[README.md → Installation](README.md#installation): GCC 15,
Clang 20, CMake 3.20+, `libudev-dev`, `libsystemd-dev`,
`libsqlite3-dev`, `libgtest-dev`, plus **sdbus-c++ 2.x** and
**eventpp** from source (the Ubuntu/Debian packages are too old or
missing). The exact commands are checked in at
[docker/Dockerfile](docker/Dockerfile) — copy from the `toolchain`
stage and you're done.

Then:

```bash
scripts/install-hooks      # points git's core.hooksPath at scripts/git-hooks/
cmake --preset gnu         # configures cmake-build-gnu/ and emits compile_commands.json
```

## 2. First build and test

```bash
cmake --build cmake-build-gnu                 # build
./cmake-build-gnu/zwaved --version            # smoke test
ctest --test-dir cmake-build-gnu --output-on-failure   # run the unit suite
```

The unit tests are tiny and finish in milliseconds — run them
freely. New `TEST()` blocks under `tests/` are auto-discovered.

Other presets you can configure on demand: `llvm`, `gnu-tidy`,
`llvm-tidy` (the `-tidy` variants fail the build on any clang-tidy
diagnostic, exactly like the pre-commit hook does on staged files).

## 3. The commit gate

`scripts/install-hooks` activates a pre-commit hook that runs
`clang-format --dry-run --Werror` and `clang-tidy --quiet
-warnings-as-errors='*'` against staged C/C++ files. CI runs the
same checks, so anything the hook lets through is fine to push.

If the hook blocks you:

```bash
scripts/check-format --fix          # auto-apply clang-format and re-stage
cmake --build cmake-build-gnu --target fix-tidy   # auto-apply clang-tidy fixes
```

Full background — including why files under `tests/` skip tidy and
how the script finds `compile_commands.json` — is in
[PreCommitHook.md](PreCommitHook.md).

## 4. Where the code lives

Skim [CLAUDE.md → Architecture](CLAUDE.md) once before you start
editing — it's the canonical map. The 30-second version:

| Directory | What it owns |
|-----------|--------------|
| `src/logger/` | Async logger (priority 101). Sink picked at build time. |
| `src/config/` | INI-style config parser (priority 102). Publishes retained events on `MessageBus`. |
| `src/zwave-dongle/` | `libudev` USB hot-plug monitor (priority 201). |
| `src/zwave-protocol/` | Serial framing, Host-API codec, send-data dispatch (priority 202). Per-CC encoders under `application/`. |
| `src/external-api/` | Transport-agnostic backend interface. `DBusBackend` is the only impl today (priority 203). |
| `src/message-bus/` | Thin wrapper over eventpp — the only coupling between threads. |
| `src/node-registry/` | SQLite-backed registry of included nodes. |
| `src/cc-translator/` | Generated CC encode/decode shims; driven by the manifest. |
| `tests/` | GoogleTest suites for the protocol-layer codecs. |
| `utils/zwave-terminal/` | ncurses TUI client over D-Bus — handy for poking at a running daemon. |

Lifecycle is driven by `__attribute__((constructor(N)))` /
`destructor(N)`; `main()` only spins on `applicationRunning`.
[CLAUDE.md](CLAUDE.md) has the full priority table and the
shutdown-ordering notes — read those *before* adding a new
constructor-priority module.

## 5. The manifest workflow

The cross-module surface — every `MessageBus` event, every D-Bus
method/signal, every CC wire constant — is declared in
[InterfaceManifest.yml](InterfaceManifest.yml). `scripts/codegen/`
reads it at build time and emits C++ into
`cmake-build-*/generated/`.

> Adding a new bus event, D-Bus method, signal, or CC is
> "edit the manifest, rebuild." Do *not* hand-write a duplicate.

See [scripts/codegen/README.md](scripts/codegen/README.md) for what
the manifest can and can't express today.

## 6. Run the daemon end-to-end (optional)

If you have an Aeotec Z-Stick Gen5 (VID `0658`, PID `0200`)
attached:

```bash
# Install the system-bus policy so non-root clients can talk to com.tiunda.ZWaved
sudo install -m 644 dbus/com.tiunda.ZWaved.conf /etc/dbus-1/system.d/
sudo systemctl reload dbus

# Run the daemon (logs to stdout under the default sink)
./cmake-build-gnu/zwaved

# In another terminal — the ncurses TUI:
./cmake-build-gnu/utils/zwave-terminal/zwave-terminal
```

Operator-facing flows (inclusion, exclusion, SmartStart, signal
payload layout, troubleshooting) are in [MANUAL.md](MANUAL.md).

## 7. A reasonable first task

Pick something small from [TODO.md](TODO.md) — most entries are
self-contained CC additions or D-Bus surface tweaks, and the
manifest-driven workflow means the diff stays narrow. If nothing
there grabs you, adding a `TEST()` for an existing codec under
`tests/` is the lowest-risk way to learn the code.
