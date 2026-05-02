"""
targets.cc_codecs — generate application/<Name>.gen.{hpp,cpp} for every
CC declared with a complete wire shape.

Inputs (from InterfaceManifest.yml):
    command_classes:           per-CC class byte, command bytes,
                               encode_args / wire (for Set/Get
                               encoders), decoded_struct (for Report
                               decoders)

Outputs (under <out>/application/):
    <Name>.gen.hpp
        - constexpr COMMAND_CLASS / <CMD>_<NAME> bytes
        - struct Report (or per-command decoded structs) for any
          command with a `decoded_struct:`
        - encode<Cmd>() declarations matching encode_args
        - decode<Cmd>() declaration returning std::optional<Report>

    <Name>.gen.cpp
        - encode<Cmd>() bodies that emit the wire bytes
          per the manifest's `wire:` expression list
        - decode<Cmd>() bodies that walk the std::span<const uint8_t>
          payload and populate the decoded struct, returning
          std::nullopt on length / class / cmd mismatch

CCs whose wire shape isn't fully expressible in the manifest today
(e.g. MultichannelAssociation's MARKER-separated REMOVE-all elision)
keep their hand-written implementation; the generator emits only a
constants/struct-shapes header for those, and the existing .cpp file
provides the irregular logic.

Templates: scripts/codegen/templates/cc_codec_hpp.j2,
           scripts/codegen/templates/cc_codec_cpp.j2

This file is a stub. Phase 5 of the rollout implements the body and
shrinks src/zwave-protocol/application/<Name>.{hpp,cpp} to its
irregular bits.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any


def generate(manifest: Any, out_dir: Path) -> list[Path]:
    raise NotImplementedError("cc-codecs target — Phase 5")
