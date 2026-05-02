"""
targets.manifest_summary — Phase 1 sanity-check target.

Emits a single header (`Manifest.gen.hpp`) carrying the manifest
version plus counts of each top-level section. Its job is to prove
the toolchain works end to end:

    YAML → loader → schema → Jinja2 template → file → clang-format

Nothing in the daemon consumes this header today; CMake still
declares it as the output of the codegen custom command so the build
graph includes the generator and a manifest edit triggers a
regeneration.

Phase 2 onward replaces this stub with real targets.
"""

from __future__ import annotations

from pathlib import Path

from jinja2 import Environment, FileSystemLoader, StrictUndefined

from schema import Manifest


TEMPLATE_DIR = Path(__file__).resolve().parent.parent / "templates"


def generate(manifest: Manifest, out_dir: Path) -> list[Path]:
    env = Environment(
        loader=FileSystemLoader(str(TEMPLATE_DIR)),
        undefined=StrictUndefined,
        keep_trailing_newline=True,
        trim_blocks=True,
        lstrip_blocks=True,
    )
    template = env.get_template("manifest_summary_hpp.j2")

    method_count = len(manifest.dbus.methods) if manifest.dbus else 0
    signal_count = len(manifest.dbus.signals) if manifest.dbus else 0

    rendered = template.render(
        manifest=manifest,
        event_count=len(manifest.events),
        struct_count=len(manifest.structs),
        method_count=method_count,
        signal_count=signal_count,
        cc_count=len(manifest.command_classes),
    )

    out_dir.mkdir(parents=True, exist_ok=True)
    out_file = out_dir / "Manifest.gen.hpp"
    out_file.write_text(rendered, encoding="utf-8")
    return [out_file]
