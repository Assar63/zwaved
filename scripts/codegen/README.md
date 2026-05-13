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
├── schema.py            dataclasses mirroring the manifest
├── loader.py            yaml → schema, validation, errors
├── targets/
│   ├── __init__.py
│   ├── messagebus.py    emits MessageBus event structs + instantiations
│   ├── dbus_methods.py  emits DBusBackend method registrations
│   ├── dbus_signals.py  emits DBusBackend signal registrations + emits
│   └── cc_codecs.py     emits application/<Name>.{hpp,cpp} skeletons
└── templates/           Jinja2 templates, one per generated output
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

### CMake integration

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

## Rollout history

The codegen landed in six phases between 2026-05-02 and 2026-05-03.
Each phase replaced one slice of hand-written code with generator
output, lowest-risk slice first.

### Phase 1 — Infrastructure stand-up ✅ (commit `33028d1`)
- `generate.py` argparse + dispatch.
- `loader.py` + `schema.py` parse `InterfaceManifest.yml` into
  validated dataclasses, with concrete errors on missing fields,
  unknown categories, dangling event refs, and malformed type
  expressions.
- A trivial sanity-check target (since removed in Phase 6) proved
  the YAML → loader → schema → Jinja2 → file → clang-format chain
  end to end before any real target plugged in.
- CMake `add_custom_command` with dependency tracking on the
  manifest, every `*.py`, and every template.

### Phase 2 — MessageBus ✅ (commit `33028d1`)
- `MessageBus.gen.hpp` carries every event struct, every nested
  struct (`NodeInfo` / `EndpointMember` / `AcceptedDongleConfig`),
  and the `IsRetained<T>` specializations.
- `MessageBus.gen.cpp` carries the explicit
  `subscribe<T>` / `publish<T>` instantiations.
- `MessageBus.hpp` shrunk from 393 to 55 lines (a thin wrapper over
  the generated header plus the public-API declarations).
  `MessageBus.cpp` shrunk from 162 to 30 lines.
- New `MessageBusInternal.hpp` holds the `Topic<T>` /
  `BusState` / template bodies in `MessageBus::detail::`, shared by
  both translation units.

### Phase 3 — D-Bus methods ✅ (commit `4afe339`)
- 13 of 22 methods (every `publish:` and `publish_constant:` action)
  now generated inline in `DBusMethods.gen.cpp`.
- 9 methods (flag-byte unpacking on `AddNode` / `RemoveNode`,
  struct-array conversion on the Multi Channel Association
  Set/Remove, plus the four `read_cached:`-shaped getters and
  `GetVersion` / `GetNetworkStatus`) stay hand-written as `custom:`
  handlers — the codegen forwards to them via thin per-method
  lambdas.
- `DBusBackendInternal.hpp` lifted the `Impl` struct, tuple aliases,
  and `BUS_NAME` / `OBJECT_PATH` / `IFACE_NAME` constants out of
  the `.cpp`.
- `DBusBackend.cpp` lost ~166 lines (the entire flat method
  registration list).

### Phase 4 — D-Bus signals ✅ (commit `035b44a`)
- All 9 D-Bus signals generated. Per-event subscribers grouped so
  one `MessageBus::subscribe<X>` can drive multiple signals (the
  `ApplicationCommand` event drives both the raw `ApplicationCommand`
  passthrough and the typed `SwitchBinaryReport` via the manifest's
  `decode:` notation, which inlines the
  `BinarySwitch::decodeReport` call and the `std::nullopt` guard).
- Five hand-written cache-update subscribers stay (DongleStatus /
  DongleInfo / InitData / NodeListChanged / SessionStatus) — they
  feed `impl->last*` for the `custom: emitGet*` handlers and run
  before the generated signal subs so any field a D-Bus signal
  carries is already cached when it goes out.
- `DBusBackend.cpp` lost another ~270 lines (the registerSignal
  block and the eight signal-emit lambdas).

### Phase 5 — CC codec skeletons ✅ (commit `c75efd1`)
- Every module gets `application/<Name>.gen.hpp` with COMMAND_CLASS,
  per-command byte constants (`<wire.prefix>_<CMD>`), and any
  per-module `constants:` block (`VALUE_OFF` / `VALUE_ON` /
  `VALUE_UNKNOWN`, `MARKER`).
- CCs with fully-expressed wire shapes (BinarySwitch, Basic) also
  get `application/<Name>.gen.cpp` with simple `encodeSet` /
  `encodeGet` bodies.
- Hand-written `application/<Name>.{hpp,cpp}` shrunk to the
  irregular parts: `enum State`, `struct Report`, decode functions,
  multi-frame Association encoders, MultichannelAssociation's
  MARKER-elision REMOVE-all wire form.

### Phase 6 — Cleanup ✅ (this rev)
- Phase 1 sanity-check target (`Manifest.gen.hpp` +
  `targets/manifest_summary.py` + its template) deleted; its job
  was to prove the toolchain wires up, and Phases 2–5 prove that
  every rebuild.
- Top-level docs (`README.md`, `CLAUDE.md`) updated to point at
  `InterfaceManifest.yml` as the canonical interface description.
- `MANUAL.md` / `README.md` method-table generation **deferred** —
  see the section below. The duplication is currently low-cost
  (~one round of doc edits per CC; no observed drift bugs in the
  six-phase rollout). The infrastructure to do it cleanly
  (auto-update markers + `--check` mode + a CI verification step)
  is a multi-day stretch that doesn't pay for itself yet.

## Deferred extensions

These are tracked here as obvious next steps for the codegen, ordered
by leverage. None are blocking; each lands when a concrete need
shows up.

### `read_cached:` action support
Three methods stay `custom:` today (`emitGetNodes`,
`emitGetDongleInfo`, `emitGetInitData`) because the codegen doesn't
yet emit the cached-state lookup + tuple-construction body. The
manifest already documents the action shape; the generator just
needs the inline-body emission and a per-event "cached-as" name
convention (NodeListChanged → `lastNodes`, DongleInfo → cached
whole, etc.).

### Flag-byte unpacking annotation
`AddNode` / `RemoveNode` stay `custom:` because the inbound `flags`
byte unpacks into four bool fields on the bus event. A small
`unpack_bits:` annotation on the param would express this:

```yaml
- { name: flags, dbus: y, cpp: u8, unpack_bits: { 7: power, 6: nwi, 5: protocolLongRange, 4: skipFlNeighbors } }
```

The codegen would emit the `bitSet(flags, 7)` calls automatically.

### Inbound struct-array conversion
The `SetMultichannelAssociation` / `RemoveMultichannelAssociation`
endpoint-pair param is `a(yy)` on the wire and
`vector<MessageBus::EndpointMember>` on the bus event. A simple
`element_struct: EndpointMember` notation (positional field
mapping from the inbound `sdbus::Struct` to the bus struct) would
let those become regular `publish:` actions.

### Variable-length wire shapes for encoders
Association's encoders take a `std::span<const uint8_t> members`
parameter and emit it as a sequence of bytes after the
`(CC, cmd, groupId)` header. A richer `wire:` grammar could express
this — splat operator, conditional MARKER emission for MCA's
REMOVE-all — and let the encoders generate.

### Decode-side support including enum mapping
`BinarySwitch::decodeReport` and `Basic::decodeReport` stay
hand-written because the manifest can't currently express the
state-byte → enum-State mapping or the v1/v2 wire-form branching
in `Basic`. Adding a `decode:` block that mirrors the existing
`encode_args:` / `wire:` pattern would close the last gap.

### Per-directory overlay files (already deferred)
See the section below — design held for the first concrete
"manifest can't express this CC's irregular bit" case.

### Markdown method-table generation (deferred from Phase 6)
A target that emits the canonical method/signal table from the
manifest into `MANUAL.md` and `README.md` via auto-update markers.
Combined with a `--check` mode the pre-commit hook runs, this
removes the last documented duplication. Practical when manifest
edits start outpacing manual doc updates; on the back burner until
then.

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
