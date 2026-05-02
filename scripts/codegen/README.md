# scripts/codegen/

Build-time code generator that consumes `InterfaceManifest.yml` and
emits the C++ glue currently hand-written across `MessageBus.{hpp,cpp}`,
`DBusBackend.cpp`, and `src/zwave-protocol/application/<Name>.{hpp,cpp}`.

The manifest is the single source of truth; the generator output is a
*derivative*. After the rollout completes, the corresponding hand-
written sections are deleted and the manifest becomes the only place
to add a new MessageBus event, D-Bus method, D-Bus signal, or
Command Class codec.

## Language: Python

Picked Python (3.11+) over the alternatives:

- **Why Python:** YAML parsing is one line; Jinja2 is the de-facto
  "structured-data → text" engine; the codegen runs at build time on
  the developer's host so the language is invisible to the daemon's
  runtime; the standard library carries everything except YAML and
  Jinja2.
- **Dependencies:** `python3` (≥ 3.11), `python3-yaml`
  (`pyyaml`), `python3-jinja2` (`Jinja2`). All three ship as stock
  packages on every supported distro and pull in nothing transitive.
- **Why not C++:** Same toolchain as the daemon, but YAML parsing
  needs `yaml-cpp`, templating gets ugly, and the build-time tool
  doesn't gain anything from sharing C++ types with the runtime.
- **Why not pure CMake:** No real string processing, no struct loop,
  no error handling worth the name.
- **Why not bash+yq:** Fine for a one-off twenty-line emitter, won't
  scale to four output kinds with shared schema validation.
- **Why not Rust / Go:** Extra toolchain for zero benefit over Python.

## Layout

```
scripts/codegen/
├── README.md            this file
├── generate.py          CLI entry point — dispatches to target modules
├── schema.py            (Phase 1) dataclasses mirroring the manifest
├── loader.py            (Phase 1) yaml → schema, validation, errors
├── targets/
│   ├── __init__.py
│   ├── messagebus.py    emits MessageBus event structs + instantiations
│   ├── dbus_methods.py  emits DBusBackend method registrations
│   ├── dbus_signals.py  emits DBusBackend signal registrations + emits
│   └── cc_codecs.py     emits application/<Name>.{hpp,cpp} skeletons
└── templates/           (Phase 1+) Jinja2 templates, one per output
```

## Usage

```bash
# Generate everything into cmake-build-*/generated/.
python3 scripts/codegen/generate.py \
    --manifest InterfaceManifest.yml \
    --out cmake-build-gnu/generated

# Generate just one target — useful while iterating on a template.
python3 scripts/codegen/generate.py \
    --manifest InterfaceManifest.yml \
    --out cmake-build-gnu/generated \
    --target messagebus
```

The generator runs `clang-format -i` on every emitted file before
exiting so the output already matches the project's style and a
post-generation diff doesn't churn on whitespace.

### CMake integration (Phase 1)

A custom command in the root `CMakeLists.txt` invokes the generator
when `InterfaceManifest.yml` or any file under `scripts/codegen/` changes.
Generated headers land under `${CMAKE_BINARY_DIR}/generated/` (already
on the daemon's include path for `Version.hpp`); generated `.cpp`
files are added to the daemon target via `target_sources`. The custom
command depends on `python3` being on `PATH` and on the two pip
packages — the configure step probes for both and fails loudly with a
"run `apt install python3-yaml python3-jinja2`" hint if either is
missing.

The generated tree is **not** committed. The single source of truth is
the manifest; the generated C++ is a derivative artefact, the same
way `cmake-build-*/generated/Version.hpp` already is. Reviewers see
manifest changes in the diff; CI proves the generator is reproducible.

## Phased rollout

Each phase replaces one slice of hand-written code with generator
output. Earlier phases pay for the infrastructure of later ones, so
the order is "lowest-risk slice first."

### Phase 1 — Infrastructure stand-up
- `codegen/generate.py` parses arguments and dispatches.
- `codegen/loader.py` loads `InterfaceManifest.yml` and validates it
  against `codegen/schema.py` dataclasses, with concrete error
  messages on missing/unknown fields and unknown type names.
- One target generates a single trivial header (a sanity-check banner)
  to prove the toolchain is wired end to end.
- CMake custom command + dependency tracking lands.
- No daemon code consumes generated output yet.

### Phase 2 — MessageBus
- Generate `MessageBus.gen.hpp` carrying every event struct, the
  `IsRetained<T>` specializations, and the nested struct definitions
  (`NodeInfo`, `EndpointMember`, `AcceptedDongleConfig`).
- Generate `MessageBus.gen.cpp` carrying the explicit instantiations.
- `MessageBus.hpp` becomes a thin file that includes
  `MessageBus.gen.hpp` and declares the public `subscribe` /
  `publish` / `unsubscribe` / `touch` API.
- Delete the hand-written event-struct and instantiation blocks.
- This is the lowest-risk slice: pure data shapes, no behaviour
  change, every existing call site continues to compile unmodified.

### Phase 3 — D-Bus methods
- The manifest's `action:` notation drives a generator that emits
  `DBusMethods.gen.cpp` containing the `obj.registerMethod(...)`
  block for every method whose action is `publish:` /
  `publish_constant:` / `read_cached:`. Methods marked
  `custom:` (today: `GetVersion`, `GetNetworkStatus`) keep their
  hand-written bodies and are referenced by the generator as
  forward declarations.
- `DBusBackend.cpp` shrinks; the long flat method-registration list
  goes away. The `NOLINTBEGIN(readability-function-cognitive-complexity)`
  band on `run()` either narrows or disappears entirely.

### Phase 4 — D-Bus signals
- Generate `DBusSignals.gen.cpp` with the signal registrations and
  the `MessageBus::subscribe` lambdas that emit them. The `decode:`
  notation handles the `SwitchBinaryReport` style typed-from-CC
  signals — the generator emits the codec call and the
  `if (!decoded) return;` guard automatically.
- All hand-written `subscribe`/`emitSignal` blocks for D-Bus signals
  go away.

### Phase 5 — CC codec skeletons
- For every CC declared in the manifest with a complete wire shape,
  generate `application/<Name>.gen.hpp` and `application/<Name>.gen.cpp`
  carrying the encode/decode functions for the simple commands
  (single-byte SET, no-arg GET, fixed-shape REPORT). The
  hand-written `application/<Name>.{hpp,cpp}` shrinks to whatever
  the generator can't express — currently the irregular bits like
  `MultichannelAssociation`'s MARKER-separated REMOVE-all encoding.
- Existing tests under `tests/<Name>_test.cpp` continue to verify
  the generated codec; no test rewrite required.

### Phase 6 — Cleanup
- Delete now-orphaned hand-written code paths.
- `MANUAL.md` / `README.md` method tables are also generated from
  the manifest (markdown target). Hand-edited prose stays put.
- The manifest, the generator, and the daemon's hand-written code
  are now disjoint: nothing in the daemon's source tree
  duplicates information the manifest holds.

## Deferred: per-directory overlay files

A natural follow-up — once the central manifest can't cleanly express
some per-instance customization — is to support per-directory overlay
files (e.g. `src/zwave-protocol/application/MultichannelAssociation.codegen.yml`)
that opt out of generation for specific commands or override field
mappings. Concrete shape we'd reach for first:

```yaml
# Skip the encoder for these commands; the .cpp file hand-implements
# them because the wire shape is irregular (MARKER elision when both
# member lists are empty for REMOVE-all).
skip_commands: [Remove]
```

Deliberately deferred until Phase 5 hits the first irregular CC. The
cost of introducing it now — two sources of truth (manifest + per-dir
overlays) that need to stay in sync, plus more files to walk to
understand the full codegen story — outweighs the benefit at the
current ~20-event / 19-method / 4-CC scale. By the time the first
real overlay case lands we'll know exactly what fields it needs
instead of guessing now.

The central manifest stays the source of truth for *data shapes*;
overlays are only ever for *per-instance opt-outs and overrides*.

## Conventions

- **Filename suffix `.gen`** marks generator output: `Foo.gen.hpp`,
  `Foo.gen.cpp`. Easier to spot in diffs and `.gitignore` rules.
- **One template per output file** under `codegen/templates/`,
  named to match.
- **Do not edit generated files.** They carry an autogen banner; the
  pre-commit hook refuses staged changes to any file matching
  `*.gen.{hpp,cpp,md}`.
- **Schema changes are breaking.** Bump `version:` in the manifest,
  call out the breaking change in the commit message.
- **Validation is loud.** Unknown type names, dangling event refs,
  duplicate names, unknown action shapes — every one of these is a
  hard error with the manifest line/column included.
