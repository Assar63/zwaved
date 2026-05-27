"""
targets.messagebus — generate MessageBus.gen.{hpp,cpp} from the
manifest's `structs:` and `events:` sections.

Outputs:

  MessageBus.gen.hpp
      - One C++ struct per `structs:` entry (NodeInfo,
        EndpointMember, AcceptedDongleConfig).
      - One C++ struct per `events:` entry, with the manifest's
        `default:` initializer where given. `constants:` blocks
        become `static constexpr std::uint8_t NAME = VALUE;` members.
      - The `IsRetained<T>` primary template (false_type) plus one
        `template <> struct IsRetained<EventName> : std::true_type {};`
        for every event whose category is `state` or `config`.

  MessageBus.gen.cpp
      - Explicit instantiation of MessageBus::subscribe<T> and
        MessageBus::publish<T> for every event in declaration order.
      - Includes MessageBusInternal.hpp so the template bodies are
        visible at the instantiation site (template definitions live
        in the hand-written internal header; only the instantiations
        are generated).

Phase 2 of the codegen rollout — see scripts/codegen/README.md for
context.
"""

from __future__ import annotations

from pathlib import Path

from jinja2 import Environment, FileSystemLoader, StrictUndefined

from schema import Field, Manifest


TEMPLATE_DIR = Path(__file__).resolve().parent.parent / "templates"


# ---- Jinja2 filters ------------------------------------------------

PRIMITIVE_CPP = {
    "bool":   "bool",
    "u8":     "std::uint8_t",
    "u16":    "std::uint16_t",
    "u32":    "std::uint32_t",
    "u64":    "std::uint64_t",
    "i32":    "std::int32_t",
    "string": "std::string",
    "bytes":  "std::vector<std::uint8_t>",
}

INTEGER_TYPES = {"u8", "u16", "u32", "u64", "i32"}


def cpp_type(type_str: str) -> str:
    """Render a manifest type expression as C++."""
    if type_str in PRIMITIVE_CPP:
        return PRIMITIVE_CPP[type_str]
    if type_str.startswith("list<") and type_str.endswith(">"):
        return f"std::vector<{cpp_type(type_str[5:-1].strip())}>"
    if type_str.startswith("array<") and type_str.endswith(">"):
        inner = type_str[len("array<"):-1].strip()
        parts = [p.strip() for p in inner.split(",")]
        if len(parts) != 2:
            raise ValueError(f"array<...> requires <T, N>: {type_str!r}")
        return f"std::array<{cpp_type(parts[0])}, {parts[1]}>"
    # Otherwise: a struct ref — emit verbatim.
    return type_str


def cpp_default_suffix(field: Field) -> str:
    """Initializer suffix for the field's struct member declaration."""
    if field.default is not None:
        return f" = {field.default}"
    if field.type == "bool":
        return " = false"
    if field.type in INTEGER_TYPES:
        return " = 0"
    if field.type.startswith("array<"):
        # std::array doesn't value-initialize its elements without an
        # explicit `{}` — empty-brace it so cppcoreguidelines-pro-
        # type-member-init stays happy.
        return "{}"
    # Strings, vectors, struct refs default-construct fine.
    return ""


# ---- Entry point ---------------------------------------------------

def generate(manifest: Manifest, out_dir: Path) -> list[Path]:
    env = Environment(
        loader=FileSystemLoader(str(TEMPLATE_DIR)),
        undefined=StrictUndefined,
        keep_trailing_newline=True,
        trim_blocks=True,
        lstrip_blocks=True,
    )
    env.filters["cpp_type"]    = cpp_type
    env.filters["cpp_default"] = cpp_default_suffix

    out_dir.mkdir(parents=True, exist_ok=True)
    out_files: list[Path] = []

    hpp = env.get_template("messagebus_hpp.j2").render(manifest=manifest)
    hpp_path = out_dir / "MessageBus.gen.hpp"
    hpp_path.write_text(hpp, encoding="utf-8")
    out_files.append(hpp_path)

    cpp = env.get_template("messagebus_cpp.j2").render(manifest=manifest)
    cpp_path = out_dir / "MessageBus.gen.cpp"
    cpp_path.write_text(cpp, encoding="utf-8")
    out_files.append(cpp_path)

    return out_files
