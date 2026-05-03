#!/usr/bin/env python3
"""
scripts/codegen/generate.py — entry point for the InterfaceManifest.yml-
driven code generator.

Reads InterfaceManifest.yml, validates it, and dispatches to the
target modules under scripts/codegen/targets/. Each target writes its
output into <out>/ and (unless --no-format) runs clang-format on the
result.

Usage:
    python3 scripts/codegen/generate.py \\
        --manifest InterfaceManifest.yml \\
        --out cmake-build-gnu/generated \\
        [--target {all,manifest-summary,messagebus,dbus-methods,dbus-signals,cc-codecs}]

`all` runs every target whose module has a real implementation.
Stub targets (messagebus / dbus-methods / dbus-signals / cc-codecs in
Phase 1) raise NotImplementedError and are skipped under `--target all`,
so the build never fails for "Phase 5 isn't implemented yet."
"""

from __future__ import annotations

import argparse
import importlib
import shutil
import subprocess
import sys
from pathlib import Path


# Each entry maps a CLI target name to (module path, "phase" tag for
# logging). Order is the order `--target all` runs them in.
TARGETS: list[tuple[str, str, str]] = [
    ("messagebus",   "targets.messagebus",   "MessageBus"),
    ("dbus-methods", "targets.dbus_methods", "D-Bus methods"),
    ("dbus-signals", "targets.dbus_signals", "D-Bus signals"),
    ("cc-codecs",    "targets.cc_codecs",    "CC codecs"),
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument(
        "--manifest",
        type=Path,
        required=True,
        help="Path to InterfaceManifest.yml",
    )
    parser.add_argument(
        "--out",
        type=Path,
        required=True,
        help="Output directory for generated files",
    )
    parser.add_argument(
        "--target",
        choices=["all", *(t[0] for t in TARGETS)],
        default="all",
        help="Generate one target only (default: all implemented targets)",
    )
    parser.add_argument(
        "--no-format",
        action="store_true",
        help="Skip the post-generation clang-format pass",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if not args.manifest.is_file():
        print(f"error: manifest not found: {args.manifest}", file=sys.stderr)
        return 2

    args.out.mkdir(parents=True, exist_ok=True)

    # Make sibling modules importable as `loader`, `schema`,
    # `targets.<name>` regardless of CWD.
    sys.path.insert(0, str(Path(__file__).parent))

    from loader import ManifestError, load

    try:
        manifest = load(args.manifest)
    except ManifestError as err:
        print(f"error: {err}", file=sys.stderr)
        return 1

    selected = [t for t in TARGETS if args.target in ("all", t[0])]

    generated: list[Path] = []
    for cli_name, module_path, phase in selected:
        try:
            module = importlib.import_module(module_path)
        except ImportError as err:
            print(f"error: cannot import {module_path}: {err}", file=sys.stderr)
            return 1

        try:
            new_files = module.generate(manifest, args.out)
        except NotImplementedError as err:
            if args.target == "all":
                # Skip silently for stubs when running "all"; fail loudly
                # when explicitly asked for a specific stub target.
                continue
            print(f"error: {cli_name}: {err}", file=sys.stderr)
            return 1

        for path in new_files:
            print(f"[codegen] {phase}: wrote {path}", file=sys.stderr)
        generated.extend(new_files)

    if not args.no_format and generated:
        _format_files(generated)

    return 0


def _format_files(files: list[Path]) -> None:
    fmt = shutil.which("clang-format")
    if fmt is None:
        print("[codegen] clang-format not found — skipping format pass", file=sys.stderr)
        return
    cpp_like = [str(p) for p in files if p.suffix in {".cpp", ".hpp", ".cc", ".h", ".hh", ".cxx"}]
    if not cpp_like:
        return
    subprocess.run([fmt, "-i", *cpp_like], check=True)


if __name__ == "__main__":
    sys.exit(main())
