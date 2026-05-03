"""
targets.cc_codecs — generate per-CC constants headers and (where the
manifest's wire shape is fully expressed) the simple encode functions.

For each command class declared in the manifest, emit one
`application/<Name>.gen.hpp`:

    namespace <Name> {
        constexpr std::uint8_t COMMAND_CLASS = <class_byte>;
        constexpr std::uint8_t <wire_prefix>_<CMD> = <byte>;  // x N
        constexpr std::uint8_t <const> = <value>;             // x M
        [[nodiscard]] auto encode<Cmd>(<encode_args>)
            -> std::vector<std::uint8_t>;
        // ... one declaration per command with an `encode_args:` /
        //     `wire:` block in the manifest.
    }

Where any command in the CC has both `encode_args:` and `wire:`,
also emit `application/<Name>.gen.cpp` with the body:

    auto <Name>::encode<Cmd>(...) -> std::vector<std::uint8_t> {
        return {COMMAND_CLASS, <wire_prefix>_<CMD>, <wire-expr>...};
    }

The hand-written `application/<Name>.{hpp,cpp}` continues to define
anything the manifest can't express -- struct Report, enum State, the
decode functions, and any irregular encoders (e.g.
MultichannelAssociation's MARKER-separated REMOVE-all wire form).

Phase 5 of the codegen rollout.
"""

from __future__ import annotations

import re
from pathlib import Path

from jinja2 import Environment, FileSystemLoader, StrictUndefined

from schema import CcCommand, CommandClass, Manifest


TEMPLATE_DIR = Path(__file__).resolve().parent.parent / "templates"


# ---- Naming helpers ----------------------------------------------

def camel_to_snake_upper(name: str) -> str:
    """`MultichannelAssociation` -> `MULTICHANNEL_ASSOCIATION`."""
    s = re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1_\2", name)
    s = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", s)
    return s.upper()


def wire_prefix(cc: CommandClass) -> str:
    """The C++ constant prefix for command bytes — uses the
    manifest's `wire_prefix:` if given (matches the Z-Wave spec wire
    name), otherwise derives from the CC name."""
    if cc.wire_prefix:
        return cc.wire_prefix
    return camel_to_snake_upper(cc.name)


def command_constant(cc: CommandClass, command: CcCommand) -> str:
    """`SWITCH_BINARY_SET` for command `Set` on CC BinarySwitch."""
    return f"{wire_prefix(cc)}_{camel_to_snake_upper(command.name)}"


# ---- C++ types in encode_args ------------------------------------

PRIMITIVE_CPP = {
    "bool": "bool",
    "u8":   "std::uint8_t",
    "u16":  "std::uint16_t",
    "u32":  "std::uint32_t",
    "u64":  "std::uint64_t",
}


def cpp_arg_type(type_str: str) -> str:
    if type_str in PRIMITIVE_CPP:
        return PRIMITIVE_CPP[type_str]
    raise ValueError(f"encode_args type {type_str!r} not supported by the cc-codecs codegen")


# ---- Per-command predicates --------------------------------------

def has_encoder(command: CcCommand) -> bool:
    """A command gets a generator-emitted encoder when its manifest
    entry declares both `encode_args:` and `wire:` (either may be
    empty for a no-arg / no-payload encoder like GET, but both keys
    must be present)."""
    return command.encode_args is not None and command.wire is not None


def cc_has_any_encoder(cc: CommandClass) -> bool:
    return any(has_encoder(c) for c in cc.commands)


# ---- Entry point -------------------------------------------------

def generate(manifest: Manifest, out_dir: Path) -> list[Path]:
    if not manifest.command_classes:
        return []

    env = Environment(
        loader=FileSystemLoader(str(TEMPLATE_DIR)),
        undefined=StrictUndefined,
        keep_trailing_newline=True,
        trim_blocks=True,
        lstrip_blocks=True,
    )
    env.filters["cpp_arg_type"] = cpp_arg_type
    env.globals["wire_prefix"]  = wire_prefix
    env.globals["command_constant"] = command_constant
    env.tests["has_encoder"] = has_encoder

    out_dir.mkdir(parents=True, exist_ok=True)
    application_dir = out_dir / "application"
    application_dir.mkdir(parents=True, exist_ok=True)

    out_files: list[Path] = []

    hpp_template = env.get_template("cc_codec_hpp.j2")
    cpp_template = env.get_template("cc_codec_cpp.j2")

    for cc in manifest.command_classes:
        hpp_path = application_dir / f"{cc.name}.gen.hpp"
        hpp_path.write_text(hpp_template.render(cc=cc), encoding="utf-8")
        out_files.append(hpp_path)

        # Only emit a .gen.cpp when the CC has at least one command
        # whose wire shape is fully expressed; otherwise the file
        # would be empty and just add link clutter.
        if cc_has_any_encoder(cc):
            cpp_path = application_dir / f"{cc.name}.gen.cpp"
            cpp_path.write_text(cpp_template.render(cc=cc), encoding="utf-8")
            out_files.append(cpp_path)

    return out_files
