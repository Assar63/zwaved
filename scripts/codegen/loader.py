"""
loader.py — parse InterfaceManifest.yml into the schema dataclasses
defined in schema.py.

Validation rules enforced here (Phase 1, kept deliberately tight):

* Top-level `version:` must be present and equal to the supported
  schema version (1 today).
* Required keys per section: events need `name` + `category` + `fields`;
  methods need `name`; signals need `name` + `params` + `triggered_by`;
  modules need `name` + a `wire:` block with `class_byte`.
* Categories restricted to {state, config, transient, command}.
* Action shapes: every `methods[].action` must contain exactly one of
  the four documented keys (publish / publish_constant / read_cached /
  custom).
* Cross-references resolve: every name referenced from an action,
  signal trigger, or constant override exists in `events:`.
* Names within a section are unique (no two events with the same name,
  etc.).
* The pre-#57 top-level keys `command_classes:` / `cc_translations:`
  raise a clear migration error.

Errors carry the manifest path and the bad line/column when PyYAML
exposes one. Anything that escapes validation here is the loader's
fault — downstream targets are allowed to assume the schema invariants
hold.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any

import yaml

from schema import (
    Action,
    ActionCustom,
    ActionPublish,
    ActionPublishConstant,
    ActionReadCached,
    Command,
    CommandWire,
    Constant,
    DBus,
    DBusParam,
    Direction,
    Event,
    Field,
    Manifest,
    Method,
    MethodReturn,
    Module,
    ModuleWire,
    Signal,
    SignalDecode,
    SignalTrigger,
    Struct,
    Translation,
    TranslationDecode,
)


SUPPORTED_VERSION = 1
VALID_CATEGORIES = {"state", "config", "transient", "command"}
ACTION_KEYS = {"publish", "publish_constant", "read_cached", "custom"}


class ManifestError(ValueError):
    """Raised on any validation failure with the manifest path included."""


def load(path: Path) -> Manifest:
    """Parse and validate `path`, returning a Manifest dataclass."""
    raw_text = path.read_text(encoding="utf-8")
    try:
        raw = yaml.safe_load(raw_text)
    except yaml.YAMLError as err:
        raise ManifestError(f"{path}: YAML parse error: {err}") from err

    if not isinstance(raw, dict):
        raise ManifestError(f"{path}: top-level must be a mapping")

    _check_legacy_keys(raw, source=path)
    return _build_manifest(raw, source=path)


# Pre-#57 top-level keys. Detect them up front and refuse with a
# migration hint rather than letting the loader run on a half-migrated
# manifest and produce confusing downstream errors.
_LEGACY_TOP_LEVEL = {
    "command_classes": "modules",
    "cc_translations": "translations",
}


def _check_legacy_keys(raw: dict, source: Path) -> None:
    bad = [k for k in _LEGACY_TOP_LEVEL if k in raw]
    if not bad:
        return
    hints = ", ".join(f"{k!r} → {_LEGACY_TOP_LEVEL[k]!r}" for k in bad)
    raise ManifestError(
        f"{source}: legacy top-level key(s) ({hints}). "
        f"This manifest was written against the pre-#57 schema. Rename the "
        f"section(s) and group per-module wire fields (class_byte, prefix) "
        f"under a nested 'wire:' block; per-command 'byte' + payload list go "
        f"under a per-command 'wire:' block."
    )


# ---- Internal builders ----------------------------------------------

def _build_manifest(raw: dict, source: Path) -> Manifest:
    version = raw.get("version")
    if version != SUPPORTED_VERSION:
        raise ManifestError(
            f"{source}: version {version!r} unsupported "
            f"(this generator handles version {SUPPORTED_VERSION})"
        )

    structs = [_build_struct(s, source) for s in raw.get("structs") or []]
    _check_unique([s.name for s in structs], "structs", source)

    events = [_build_event(e, source) for e in raw.get("events") or []]
    _check_unique([e.name for e in events], "events", source)

    dbus_raw = raw.get("dbus")
    dbus = _build_dbus(dbus_raw, source) if dbus_raw is not None else None

    modules = [_build_module(m, source) for m in raw.get("modules") or []]
    _check_unique([m.name for m in modules], "modules", source)

    translations = [_build_translation(t, source) for t in raw.get("translations") or []]

    manifest = Manifest(
        version=version,
        structs=structs,
        events=events,
        dbus=dbus,
        modules=modules,
        translations=translations,
    )

    _check_cross_refs(manifest, source)
    return manifest


def _build_translation(raw: dict, source: Path) -> Translation:
    publishes = _require(raw, "publishes", "translation", source)
    where = f"translation publishes={publishes!r}"
    triggered_by = _require(raw, "triggered_by", where, source)
    decode_raw = _require(raw, "decode", where, source)
    decode = TranslationDecode(
        codec=_require(decode_raw, "codec", f"{where} decode", source),
        input=_require(decode_raw, "input", f"{where} decode", source),
        on_none=decode_raw.get("on_none", "skip"),
    )
    return Translation(
        publishes=publishes,
        triggered_by=triggered_by,
        decode=decode,
        map=dict(raw.get("map") or {}),
    )


def _build_struct(raw: dict, source: Path) -> Struct:
    name = _require(raw, "name", "struct", source)
    return Struct(
        name=name,
        doc=raw.get("doc"),
        fields=[_build_field(f, f"struct {name}", source) for f in raw.get("fields") or []],
    )


def _build_event(raw: dict, source: Path) -> Event:
    name = _require(raw, "name", "event", source)
    category = _require(raw, "category", f"event {name}", source)
    if category not in VALID_CATEGORIES:
        raise ManifestError(
            f"{source}: event {name!r}: category {category!r} not one of "
            f"{sorted(VALID_CATEGORIES)}"
        )
    direction = None
    if (d := raw.get("direction")) is not None:
        direction = Direction(
            publishers=list(d.get("publishers") or []),
            subscribers=list(d.get("subscribers") or []),
        )
    return Event(
        name=name,
        category=category,
        direction=direction,
        doc=raw.get("doc"),
        fields=[_build_field(f, f"event {name}", source) for f in raw.get("fields") or []],
        constants=[_build_constant(c, f"event {name}", source) for c in raw.get("constants") or []],
    )


def _build_field(raw: dict, where: str, source: Path) -> Field:
    name = _require(raw, "name", where, source)
    type_ = _require(raw, "type", f"{where} field {name}", source)
    # YAML flow-style parsing splits `{ type: array<u8, 4> }` on the
    # comma. Quote the type in the manifest to keep it whole; we
    # detect the corrupted shape here rather than emitting `array<u8`
    # as if it were a struct ref and producing unhelpful C++ errors
    # downstream.
    if type_.count("<") != type_.count(">"):
        raise ManifestError(
            f"{source}: {where} field {name!r}: malformed type {type_!r} "
            f"(unbalanced angle brackets — quote the type if it contains a comma)"
        )
    return Field(
        name=name,
        type=type_,
        default=_stringify_default(raw.get("default")),
        doc=raw.get("doc"),
    )


def _build_constant(raw: dict, where: str, source: Path) -> Constant:
    name = _require(raw, "name", f"{where} constant", source)
    value = _require(raw, "value", f"{where} constant {name}", source)
    return Constant(name=name, value=_stringify_default(value))


def _build_dbus(raw: dict, source: Path) -> DBus:
    bus_name    = _require(raw, "bus_name",    "dbus", source)
    object_path = _require(raw, "object_path", "dbus", source)
    interface   = _require(raw, "interface",   "dbus", source)

    methods = [_build_method(m, source) for m in raw.get("methods") or []]
    _check_unique([m.name for m in methods], "dbus.methods", source)

    signals = [_build_signal(s, source) for s in raw.get("signals") or []]
    _check_unique([s.name for s in signals], "dbus.signals", source)

    return DBus(
        bus_name=bus_name,
        object_path=object_path,
        interface=interface,
        methods=methods,
        signals=signals,
    )


def _build_method(raw: dict, source: Path) -> Method:
    name = _require(raw, "name", "method", source)
    where = f"method {name}"
    params = [_build_dbus_param(p, where, source) for p in raw.get("params") or []]
    returns = _build_method_return(raw.get("returns"), where, source) if raw.get("returns") else None
    action  = _build_action(raw.get("action"), where, source) if raw.get("action") else None
    return Method(name=name, params=params, returns=returns, action=action, doc=raw.get("doc"))


def _build_dbus_param(raw: dict, where: str, source: Path) -> DBusParam:
    name = _require(raw, "name", where, source)
    dbus = _require(raw, "dbus", f"{where} param {name}", source)
    return DBusParam(
        name=name,
        dbus=dbus,
        cpp=raw.get("cpp"),
        doc=raw.get("doc"),
        source=raw.get("source"),
    )


def _build_method_return(raw: dict, where: str, source: Path) -> MethodReturn:
    if "struct" in raw:
        return MethodReturn(
            struct=[_build_dbus_param(p, f"{where} return", source) for p in raw["struct"]],
        )
    return MethodReturn(type=raw.get("type"), dbus=raw.get("dbus"))


def _build_action(raw: dict, where: str, source: Path) -> Action:
    keys = set(raw.keys()) & ACTION_KEYS
    if len(keys) != 1:
        raise ManifestError(
            f"{source}: {where} action must contain exactly one of "
            f"{sorted(ACTION_KEYS)} (got {sorted(raw.keys())})"
        )
    (key,) = keys
    if key == "publish":
        return ActionPublish(event=raw[key])
    if key == "publish_constant":
        body = raw[key]
        return ActionPublishConstant(
            event=_require(body, "event", f"{where} publish_constant", source),
            fields=dict(body.get("fields") or {}),
        )
    if key == "read_cached":
        return ActionReadCached(event=raw[key])
    return ActionCustom(handler=raw[key])  # custom


def _build_signal(raw: dict, source: Path) -> Signal:
    name = _require(raw, "name", "signal", source)
    where = f"signal {name}"
    params = [_build_dbus_param(p, where, source) for p in raw.get("params") or []]

    trig_raw = _require(raw, "triggered_by", where, source)
    decode = None
    if (d := trig_raw.get("decode")) is not None:
        decode = SignalDecode(
            codec=_require(d, "codec", f"{where} decode", source),
            input=_require(d, "input", f"{where} decode", source),
            on_none=d.get("on_none", "skip"),
        )
    trigger = SignalTrigger(
        event=_require(trig_raw, "event", f"{where} triggered_by", source),
        decode=decode,
    )

    # `map:` lives inside the `triggered_by:` block per the manifest's
    # documented layout — it scopes to the trigger, not to the signal
    # as a whole. Pull it up onto the Signal dataclass for ergonomic
    # access by the codegen, but read it from the right place.
    return Signal(
        name=name,
        params=params,
        triggered_by=trigger,
        map=dict(trig_raw.get("map") or {}),
        doc=raw.get("doc"),
    )


def _build_module(raw: dict, source: Path) -> Module:
    name = _require(raw, "name", "module", source)
    where = f"module {name}"
    wire_raw = _require(raw, "wire", where, source)
    if not isinstance(wire_raw, dict):
        raise ManifestError(f"{source}: {where}: 'wire' must be a mapping")
    wire = ModuleWire(
        class_byte=_to_int(_require(wire_raw, "class_byte", f"{where} wire", source),
                           f"{where} wire", source),
        prefix=wire_raw.get("prefix"),
    )
    return Module(
        name=name,
        wire=wire,
        description=raw.get("description"),
        constants=[_build_constant(c, where, source) for c in raw.get("constants") or []],
        commands=[_build_command(c, where, source) for c in raw.get("commands") or []],
    )


def _build_command(raw: dict, where: str, source: Path) -> Command:
    name = _require(raw, "name", f"{where} command", source)
    sub_where = f"{where} command {name}"
    wire_raw = _require(raw, "wire", sub_where, source)
    if not isinstance(wire_raw, dict):
        raise ManifestError(f"{source}: {sub_where}: 'wire' must be a mapping")
    payload = None
    if "payload" in wire_raw:
        payload = list(wire_raw["payload"]) if wire_raw["payload"] is not None else []
    wire = CommandWire(
        byte=_to_int(_require(wire_raw, "byte", f"{sub_where} wire", source),
                     f"{sub_where} wire", source),
        payload=payload,
    )
    encode_args = None
    if "encode_args" in raw:
        encode_args = [_build_field(f, sub_where, source) for f in raw["encode_args"] or []]
    decoded_struct = None
    if "decoded_struct" in raw:
        decoded_struct = [_build_field(f, sub_where, source) for f in raw["decoded_struct"] or []]
    return Command(
        name=name,
        wire=wire,
        encode_args=encode_args,
        decoded_struct=decoded_struct,
    )


# ---- Cross-reference + utility ---------------------------------------

def _check_cross_refs(manifest: Manifest, source: Path) -> None:
    event_names = {e.name for e in manifest.events}

    if manifest.dbus is None:
        return

    for method in manifest.dbus.methods:
        if method.action is None:
            continue
        ev = getattr(method.action, "event", None) or getattr(method.action, "handler", None)
        if isinstance(method.action, (ActionPublish, ActionPublishConstant, ActionReadCached)):
            if method.action.event not in event_names:
                raise ManifestError(
                    f"{source}: method {method.name!r} action references "
                    f"unknown event {method.action.event!r}"
                )
        if isinstance(method.action, ActionPublishConstant):
            event = manifest.event_by_name(method.action.event)
            event_field_names = {f.name for f in event.fields}
            for fname in method.action.fields:
                if fname not in event_field_names:
                    raise ManifestError(
                        f"{source}: method {method.name!r} publish_constant "
                        f"sets unknown field {fname!r} on event {event.name!r}"
                    )

    for signal in manifest.dbus.signals:
        if signal.triggered_by.event not in event_names:
            raise ManifestError(
                f"{source}: signal {signal.name!r} triggered_by references "
                f"unknown event {signal.triggered_by.event!r}"
            )

    for translation in manifest.translations:
        if translation.publishes not in event_names:
            raise ManifestError(
                f"{source}: translation publishes references unknown event "
                f"{translation.publishes!r}"
            )
        if translation.triggered_by not in event_names:
            raise ManifestError(
                f"{source}: translation publishes={translation.publishes!r} "
                f"triggered_by references unknown event "
                f"{translation.triggered_by!r}"
            )
        published_event = manifest.event_by_name(translation.publishes)
        published_field_names = {f.name for f in published_event.fields}
        for fname in translation.map:
            if fname not in published_field_names:
                raise ManifestError(
                    f"{source}: translation publishes={translation.publishes!r} "
                    f"map sets unknown field {fname!r}"
                )


def _check_unique(names: list[str], where: str, source: Path) -> None:
    seen: set[str] = set()
    for n in names:
        if n in seen:
            raise ManifestError(f"{source}: duplicate name {n!r} in {where}")
        seen.add(n)


def _require(raw: dict, key: str, where: str, source: Path) -> Any:
    if key not in raw:
        raise ManifestError(f"{source}: {where} missing required key {key!r}")
    return raw[key]


def _stringify_default(value: Any) -> Optional[str]:
    if value is None:
        return None
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, int):
        # PyYAML parses 0xFF as int(255), losing the lexical hex
        # form. Re-emit u8-range ints as `0x%02X` to keep CC sentinels
        # (VALUE_OFF / VALUE_ON / MODE_ANY_NODE / MARKER / …) readable
        # in the generated C++. Larger ints stay decimal.
        if 0 <= value <= 0xFF:
            return f"0x{value:02X}"
        return str(value)
    return str(value)


def _to_int(value: Any, where: str, source: Path) -> int:
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        try:
            return int(value, 0)
        except ValueError as err:
            raise ManifestError(f"{source}: {where}: cannot parse integer {value!r}") from err
    raise ManifestError(f"{source}: {where}: expected integer, got {type(value).__name__}")


# Optional import shim so this module imports cleanly even when the
# stdlib doesn't include Optional in the runtime version. Python 3.11+
# always has it.
from typing import Optional  # noqa: E402  (intentional late import)
