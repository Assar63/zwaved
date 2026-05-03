"""
targets.dbus_signals — generate D-Bus signal registrations and the
MessageBus subscribers that emit them.

Outputs:

  DBusSignals.gen.hpp
      - registerGeneratedSignals(sdbus::IObject&) declaration
      - subscribeGeneratedSignals(DBusBackend::Impl&) declaration
      - unsubscribeGeneratedSignals(DBusBackend::Impl&) declaration

  DBusSignals.gen.cpp
      - registerGeneratedSignals: one obj.registerSignal(...).
        onInterface(IFACE_NAME).withParameters<...>(...) call per
        signal in the manifest.
      - subscribeGeneratedSignals: one
        MessageBus::subscribe<MessageBus::<TriggerEvent>>(...) per
        unique trigger event. Each subscriber's lambda emits every
        signal that's `triggered_by` that event:
          - Signals without `decode:` -- emitted unconditionally.
          - Signals with `decode:` -- wrapped in
            `if (auto report = <Codec>::<fn>(event.<input>);
                 report.has_value())`
            with `const auto& decoded = *report;` aliasing the value.
        SubscriptionIds are appended to
        impl.generatedSignalSubs (a vector<SubscriptionId> on Impl).
      - unsubscribeGeneratedSignals iterates that vector and clears
        it; called from DBusBackend::stop() before the cache-update
        subscribers are released.

Phase 4 of the codegen rollout. The five hand-written cache-update
subscribers (DongleStatus / DongleInfo / InitData / SessionStatus /
NodeListChanged) stay in DBusBackend.cpp -- they own the
impl.lastFoo field updates that the `custom: emitGet*` handlers
read from. Generated subscribers run after the cache-update ones,
so any field a D-Bus signal carries is already cached by the time
the signal goes out.
"""

from __future__ import annotations

import re
from collections import OrderedDict
from pathlib import Path

from jinja2 import Environment, FileSystemLoader, StrictUndefined

from schema import Manifest, Signal


TEMPLATE_DIR = Path(__file__).resolve().parent.parent / "templates"


# Manifest-side type aliases that may appear in `map:` expressions for
# `decode:`-style signals. Translated to C++ verbatim before rendering.
TYPE_ALIAS_REPLACEMENTS = (
    (re.compile(r"\bu8\b"),     "std::uint8_t"),
    (re.compile(r"\bu16\b"),    "std::uint16_t"),
    (re.compile(r"\bu32\b"),    "std::uint32_t"),
    (re.compile(r"\bu64\b"),    "std::uint64_t"),
    (re.compile(r"\bbytes\b"),  "std::vector<std::uint8_t>"),
    (re.compile(r"\bstring\b"), "std::string"),
)


def cpp_expr(expr: str) -> str:
    """Render a manifest expression with manifest-side type aliases
    (u8 / u16 / etc.) translated to their C++ counterparts. Used for
    map: clauses on `decode:` signals."""
    for pattern, replacement in TYPE_ALIAS_REPLACEMENTS:
        expr = pattern.sub(replacement, expr)
    return expr


def cpp_param_type(param) -> str:
    """C++ type for a D-Bus signal parameter."""
    direct = {
        "y":  "std::uint8_t",
        "q":  "std::uint16_t",
        "u":  "std::uint32_t",
        "t":  "std::uint64_t",
        "b":  "bool",
        "s":  "std::string",
        "ay": "std::vector<std::uint8_t>",
    }
    if param.dbus in direct:
        return direct[param.dbus]
    raise ValueError(
        f"signal param {param.name!r}: D-Bus signature {param.dbus!r} "
        f"not supported by the dbus-signals codegen yet"
    )


def codec_call(codec_dotted: str) -> str:
    """`BinarySwitch.decodeReport` -> `BinarySwitch::decodeReport`."""
    return codec_dotted.replace(".", "::")


def codec_namespace(codec_dotted: str) -> str:
    """The leading namespace of a codec, used to derive the include path."""
    return codec_dotted.split(".", 1)[0]


def map_expression(signal: Signal, param_name: str) -> str:
    """Resolve the C++ expression used to fill a signal param.

    Resolution order:
      1. `triggered_by.map:` entry (used by `decode:` signals to
         build expressions over `decoded.*`).
      2. `params[*].source:` field — explicit override that names a
         different event field than the param name (used for the
         basicType ↔ basicDeviceType etc. renames on the
         NodeInclusionStatus / NodeExclusionStatus signals).
      3. Default: `event.<param_name>` — the param name matches an
         event field 1:1.
    """
    if signal.map and param_name in signal.map:
        return cpp_expr(signal.map[param_name])
    for p in signal.params:
        if p.name == param_name and p.source:
            return f"event.{p.source}"
    return f"event.{param_name}"


def signals_by_trigger(manifest: Manifest) -> "OrderedDict[str, list[Signal]]":
    """Group signals by their trigger event, preserving manifest order."""
    grouped: "OrderedDict[str, list[Signal]]" = OrderedDict()
    if manifest.dbus is None:
        return grouped
    for signal in manifest.dbus.signals:
        grouped.setdefault(signal.triggered_by.event, []).append(signal)
    return grouped


def codec_includes(manifest: Manifest) -> list[str]:
    """The application/<Codec>.hpp include paths needed for `decode:`."""
    seen: list[str] = []
    if manifest.dbus is None:
        return seen
    for signal in manifest.dbus.signals:
        decode = signal.triggered_by.decode
        if decode is None:
            continue
        ns = codec_namespace(decode.codec)
        path = f"application/{ns}.hpp"
        if path not in seen:
            seen.append(path)
    return seen


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
    env.filters["cpp_param_type"] = cpp_param_type

    grouped = signals_by_trigger(manifest)
    includes = codec_includes(manifest)

    out_dir.mkdir(parents=True, exist_ok=True)
    out_files: list[Path] = []

    hpp_path = out_dir / "DBusSignals.gen.hpp"
    hpp_path.write_text(
        env.get_template("dbus_signals_hpp.j2").render(manifest=manifest),
        encoding="utf-8",
    )
    out_files.append(hpp_path)

    cpp_path = out_dir / "DBusSignals.gen.cpp"
    cpp_path.write_text(
        env.get_template("dbus_signals_cpp.j2").render(
            manifest=manifest,
            grouped=grouped,
            codec_includes=includes,
            codec_call=codec_call,
            map_expression=map_expression,
        ),
        encoding="utf-8",
    )
    out_files.append(cpp_path)

    return out_files
