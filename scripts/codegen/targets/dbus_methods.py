"""
targets.dbus_methods — generate the D-Bus method registration block.

Outputs:

  DBusMethods.gen.hpp
      - Forward-declarations of every `custom:` handler the manifest
        names. Each handler returns the method's declared return
        type and takes (Impl&, <inbound D-Bus args...>) by value.
      - Declaration of the entry point
        `auto registerGeneratedMethods(sdbus::IObject&,
            DBusBackend::Impl&) -> void;`

  DBusMethods.gen.cpp
      - Body of `registerGeneratedMethods`. For every method in the
        manifest:
          publish: <Event>
              -> inline lambda that constructs the named bus event
                 from the inbound args (by-name field mapping) and
                 calls MessageBus::publish(...)
          publish_constant: { event: <E>, fields: {<f>: <v>} }
              -> same as publish: with the named fields fixed at
                 the given constants (events scope: <E>::<v>)
          custom: <handler>
              -> forwards to the handler declared in the .gen.hpp;
                 the handler implementation lives by hand in
                 DBusBackend.cpp

`read_cached:` is documented in the manifest's notation but treated
as `custom:` for now — the inline tuple construction it implies needs
richer codegen support and is deferred.
"""

from __future__ import annotations

from pathlib import Path

from jinja2 import Environment, FileSystemLoader, StrictUndefined

from schema import (
    ActionCustom,
    ActionPublish,
    ActionPublishConstant,
    DBusParam,
    Manifest,
    Method,
    MethodReturn,
)
from targets.messagebus import cpp_type


TEMPLATE_DIR = Path(__file__).resolve().parent.parent / "templates"


# ---- Jinja2 helpers -------------------------------------------------

def cpp_param_type(param: DBusParam) -> str:
    """C++ type for a D-Bus method's inbound parameter."""
    if param.cpp:
        # The manifest already names the C++ type for non-trivial
        # mappings (e.g. cpp: list<EndpointMember> for an a(yy) param);
        # honour it.
        return cpp_type(param.cpp)
    return _dbus_to_cpp(param.dbus)


PRIMITIVE_PARAM_TYPES = {"bool", "std::uint8_t", "std::uint16_t", "std::uint32_t", "std::uint64_t"}


def cpp_param_decl(param: DBusParam) -> str:
    """Render `[const T&] name` for a custom-handler forward declaration.

    Primitives go by value; vectors / strings / structs go by const
    reference so clang-tidy's performance-unnecessary-value-param
    stays quiet.
    """
    cpp = cpp_param_type(param)
    if cpp in PRIMITIVE_PARAM_TYPES:
        return f"{cpp} {param.name}"
    return f"const {cpp}& {param.name}"


def cpp_return_type(returns: MethodReturn | None) -> str:
    if returns is None:
        return "void"
    if returns.struct:
        # Inline struct returns get hand-written sdbus::Struct<...>
        # tuple aliases declared in DBusBackendInternal.hpp; the
        # convention is `<MethodName>Tuple` per the existing daemon.
        # The codegen plugs the alias name in via `tuple_alias` filter
        # below — this branch is only reached when the manifest
        # didn't provide one explicitly, in which case we fall back
        # to inline expansion.
        elements = ", ".join(_dbus_to_cpp(p.dbus) for p in returns.struct)
        return f"sdbus::Struct<{elements}>"
    if returns.type:
        return cpp_type(returns.type)
    return "void"


def _dbus_to_cpp(dbus_sig: str) -> str:
    """Tiny fallback mapping for D-Bus type strings without explicit cpp."""
    direct = {
        "y":  "std::uint8_t",
        "q":  "std::uint16_t",
        "u":  "std::uint32_t",
        "t":  "std::uint64_t",
        "b":  "bool",
        "s":  "std::string",
        "ay": "std::vector<std::uint8_t>",
    }
    if dbus_sig in direct:
        return direct[dbus_sig]
    # Bare struct (`(yy)`) and array-of-struct (`a(yy)`) are uncommon
    # in inbound params; the manifest declares cpp: explicitly when
    # they show up. Fall back loudly so a missing cpp: is obvious.
    raise ValueError(f"D-Bus signature {dbus_sig!r} needs an explicit cpp: alias on the param")


# Mapping from method name to the hand-written tuple alias that
# represents its return shape. Hand-keyed for now; revisit when
# read_cached: lands.
RETURN_TUPLE_ALIASES: dict[str, str] = {
    "GetNodes":         "NodeTuple",
    "GetDongleInfo":    "DongleInfoTuple",
    "GetInitData":      "InitDataTuple",
    "GetVersion":       "DaemonVersionTuple",
    "GetNetworkStatus": "NetworkStatusTuple",
}


def method_return(method: Method) -> str:
    """C++ return type for a method, preferring the named tuple alias."""
    if method.name in RETURN_TUPLE_ALIASES:
        alias = RETURN_TUPLE_ALIASES[method.name]
        if method.returns and method.returns.type and method.returns.type.startswith("list<"):
            return f"std::vector<{alias}>"
        return alias
    return cpp_return_type(method.returns)


# ---- Action helpers ------------------------------------------------

def is_publish(method: Method) -> bool:
    return isinstance(method.action, ActionPublish)


def is_publish_constant(method: Method) -> bool:
    return isinstance(method.action, ActionPublishConstant)


def is_custom(method: Method) -> bool:
    return isinstance(method.action, ActionCustom)


def publish_event(method: Method) -> str:
    return method.action.event  # type: ignore[union-attr]


def publish_constant_overrides(method: Method) -> dict[str, str]:
    """The fixed `{field: value}` overrides for a publish_constant action."""
    action = method.action
    assert isinstance(action, ActionPublishConstant)
    out: dict[str, str] = {}
    for field, value in action.fields.items():
        # Constant references on event-scoped names need the namespace
        # prefix. Plain int / hex literals stay as-is.
        if value.replace("_", "").isalpha() or any(ch.isalpha() for ch in value):
            out[field] = f"MessageBus::{action.event}::{value}"
        else:
            out[field] = value
    return out


def custom_handler(method: Method) -> str:
    return method.action.handler  # type: ignore[union-attr]


# ---- Entry point ---------------------------------------------------

def generate(manifest: Manifest, out_dir: Path) -> list[Path]:
    if manifest.dbus is None:
        return []

    env = Environment(
        loader=FileSystemLoader(str(TEMPLATE_DIR)),
        undefined=StrictUndefined,
        keep_trailing_newline=True,
        trim_blocks=True,
        lstrip_blocks=True,
    )
    env.filters["cpp_param_type"]  = cpp_param_type
    env.filters["cpp_param_decl"]  = cpp_param_decl
    env.filters["cpp_return_type"] = cpp_return_type
    env.filters["method_return"]   = method_return
    env.tests["publish"]           = is_publish
    env.tests["publish_constant"]  = is_publish_constant
    env.tests["custom"]            = is_custom
    env.globals["publish_event"]                = publish_event
    env.globals["publish_constant_overrides"]   = publish_constant_overrides
    env.globals["custom_handler"]               = custom_handler

    out_dir.mkdir(parents=True, exist_ok=True)
    out_files: list[Path] = []

    hpp_path = out_dir / "DBusMethods.gen.hpp"
    hpp_path.write_text(
        env.get_template("dbus_methods_hpp.j2").render(manifest=manifest),
        encoding="utf-8",
    )
    out_files.append(hpp_path)

    cpp_path = out_dir / "DBusMethods.gen.cpp"
    cpp_path.write_text(
        env.get_template("dbus_methods_cpp.j2").render(manifest=manifest),
        encoding="utf-8",
    )
    out_files.append(cpp_path)

    return out_files
