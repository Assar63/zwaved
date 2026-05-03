"""
targets.cc_translator — generate CcTranslator.gen.cpp.

Reads `cc_translations:` from the manifest and emits the body of the
cc-translator module: subscribers grouped by trigger event, each
running the named codec on the inbound event field and publishing the
typed event populated via the rule's `map:` clause.

Output (under <out>/):
    CcTranslator.gen.cpp
        - File-local State struct holding a vector of subscription
          IDs; static-state destructor releases them on shutdown
          (matches the static-state-destructor pattern other modules
          use to avoid the __cxa_atexit / __attribute__((destructor))
          ordering trap).
        - One on<TriggerEvent>(...) handler per unique
          `triggered_by:` value; the body holds an
          `if (auto report = <Codec>::<fn>(event.<input>);
               report.has_value())` block per rule pointing at that
          trigger, with the typed event constructed from `map:`.
        - `__attribute__((constructor(CONFIG_CC_TRANSLATOR_PRIO)))`
          subscribe() that registers each handler with MessageBus.

The hand-written src/cc-translator/CcTranslator.cpp is replaced by
this generated TU. The translator module's directory keeps just a
CMakeLists.txt + the generated source; no application/ codec
includes leak outside the generated artefact.
"""

from __future__ import annotations

from collections import OrderedDict
from pathlib import Path

from jinja2 import Environment, FileSystemLoader, StrictUndefined

from schema import CcTranslation, Manifest


TEMPLATE_DIR = Path(__file__).resolve().parent.parent / "templates"


# Manifest-side type aliases that may appear in `map:` expressions.
# Reuse the dbus_signals helper rather than duplicate the table.
from targets.dbus_signals import cpp_expr  # noqa: E402  (after path fixup)


def codec_call(codec_dotted: str) -> str:
    """`BinarySwitch.decodeReport` → `BinarySwitch::decodeReport`."""
    return codec_dotted.replace(".", "::")


def codec_namespace(codec_dotted: str) -> str:
    return codec_dotted.split(".", 1)[0]


def map_expression(translation: CcTranslation, field_name: str) -> str:
    """C++ expression for a typed-event field. Defaults to
    `event.<name>` when the rule's map: doesn't override it."""
    if translation.map and field_name in translation.map:
        return cpp_expr(translation.map[field_name])
    return f"event.{field_name}"


def grouped_by_trigger(manifest: Manifest) -> "OrderedDict[str, list[CcTranslation]]":
    grouped: "OrderedDict[str, list[CcTranslation]]" = OrderedDict()
    for translation in manifest.cc_translations:
        grouped.setdefault(translation.triggered_by, []).append(translation)
    return grouped


def codec_includes(manifest: Manifest) -> list[str]:
    """`application/<Name>.hpp` paths for every codec referenced by a rule."""
    seen: list[str] = []
    for translation in manifest.cc_translations:
        path = f"application/{codec_namespace(translation.decode.codec)}.hpp"
        if path not in seen:
            seen.append(path)
    return seen


def generate(manifest: Manifest, out_dir: Path) -> list[Path]:
    if not manifest.cc_translations:
        return []

    env = Environment(
        loader=FileSystemLoader(str(TEMPLATE_DIR)),
        undefined=StrictUndefined,
        keep_trailing_newline=True,
        trim_blocks=True,
        lstrip_blocks=True,
    )

    out_dir.mkdir(parents=True, exist_ok=True)

    rendered = env.get_template("cc_translator_cpp.j2").render(
        manifest=manifest,
        grouped=grouped_by_trigger(manifest),
        codec_includes=codec_includes(manifest),
        codec_call=codec_call,
        map_expression=map_expression,
        # Each typed event's field list is needed so the template can
        # iterate fields in declaration order (designated initializer
        # ordering must match the struct's declaration order).
        event_by_name={ev.name: ev for ev in manifest.events},
    )
    out_path = out_dir / "CcTranslator.gen.cpp"
    out_path.write_text(rendered, encoding="utf-8")
    return [out_path]
