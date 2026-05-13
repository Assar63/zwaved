"""
targets.cc_codecs — generate per-module constants headers and (where
the manifest's wire shape is fully expressed) the simple encode
functions.

For each module declared in the manifest, emit one
`application/<Name>.gen.hpp`:

    namespace <Name> {
        constexpr std::uint8_t COMMAND_CLASS = <wire.class_byte>;
        constexpr std::uint8_t <wire.prefix>_<CMD> = <cmd.wire.byte>;  // x N
        constexpr std::uint8_t <const> = <value>;                      // x M
        [[nodiscard]] auto encode<Cmd>(<encode_args>)
            -> std::vector<std::uint8_t>;
        // ... one declaration per command with an `encode_args:` /
        //     `wire.payload:` block in the manifest.
    }

Where any command in the module has both `encode_args:` and a
`wire.payload:` list, also emit `application/<Name>.gen.cpp` with the
body:

    auto <Name>::encode<Cmd>(...) -> std::vector<std::uint8_t> {
        return {COMMAND_CLASS, <wire.prefix>_<CMD>, <payload-expr>...};
    }

The hand-written `application/<Name>.{hpp,cpp}` continues to define
anything the manifest can't express -- struct Report, enum State, the
decode functions, and any irregular encoders (e.g.
MultichannelAssociation's MARKER-separated REMOVE-all wire form).
"""

from __future__ import annotations

import re
from pathlib import Path

from jinja2 import Environment, FileSystemLoader, StrictUndefined

from schema import Command, Manifest, Module


TEMPLATE_DIR = Path(__file__).resolve().parent.parent / "templates"


# ---- Naming helpers ----------------------------------------------

def camel_to_snake_upper(name: str) -> str:
    """`MultichannelAssociation` -> `MULTICHANNEL_ASSOCIATION`."""
    s = re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1_\2", name)
    s = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", s)
    return s.upper()


def wire_prefix(module: Module) -> str:
    """The C++ constant prefix for command bytes — uses the manifest's
    `wire.prefix:` if given (matches the Z-Wave spec wire name),
    otherwise derives from the module name."""
    if module.wire.prefix:
        return module.wire.prefix
    return camel_to_snake_upper(module.name)


def command_constant(module: Module, command: Command) -> str:
    """`SWITCH_BINARY_SET` for command `Set` on module BinarySwitch."""
    return f"{wire_prefix(module)}_{camel_to_snake_upper(command.name)}"


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

def has_encoder(command: Command) -> bool:
    """A command gets a generator-emitted encoder when its manifest
    entry declares both `encode_args:` and a `wire.payload:` list
    (either may be empty for a no-arg / no-payload encoder like GET,
    but both keys must be present)."""
    return command.encode_args is not None and command.wire.payload is not None


def module_has_any_encoder(module: Module) -> bool:
    return any(has_encoder(c) for c in module.commands)


# ---- Entry point -------------------------------------------------

def generate(manifest: Manifest, out_dir: Path) -> list[Path]:
    if not manifest.modules:
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

    for module in manifest.modules:
        hpp_path = application_dir / f"{module.name}.gen.hpp"
        hpp_path.write_text(hpp_template.render(module=module), encoding="utf-8")
        out_files.append(hpp_path)

        # Only emit a .gen.cpp when the module has at least one
        # command whose wire shape is fully expressed; otherwise the
        # file would be empty and just add link clutter.
        if module_has_any_encoder(module):
            cpp_path = application_dir / f"{module.name}.gen.cpp"
            cpp_path.write_text(cpp_template.render(module=module), encoding="utf-8")
            out_files.append(cpp_path)

    return out_files
