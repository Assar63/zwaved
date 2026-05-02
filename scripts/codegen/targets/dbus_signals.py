"""
targets.dbus_signals — generate DBusSignals.gen.cpp.

Inputs (from InterfaceManifest.yml):
    dbus.signals:              every D-Bus signal, with a
                               `triggered_by:` clause naming the
                               MessageBus event that drives it.
                               Optional `decode:` runs an
                               application/ codec on a named field
                               (today: SwitchBinaryReport's
                               BinarySwitch.decodeReport on ccData).

Outputs (under <out>/):
    DBusSignals.gen.cpp
        - one `obj.registerSignal(...).onInterface(...).withParameters<...>(...)`
          invocation per signal
        - one `MessageBus::subscribe<TriggerEvent>([this](...) { emit })`
          lambda per signal, with the codec call + std::nullopt guard
          inlined when `decode:` is present
        - the matching `MessageBus::unsubscribe(impl->fooSub)` calls
          for stop()

Replaces (after Phase 4 lands):
    - the `obj.registerSignal(...)` block in DBusBackend::run()
    - the corresponding `MessageBus::subscribe<...>(...)` lambdas
      that emit the signals
    - the `if (impl->fooSub != 0) { MessageBus::unsubscribe(...); }`
      pairs in DBusBackend::stop()

Template: scripts/codegen/templates/dbus_signals_cpp.j2

This file is a stub. Phase 4 of the rollout implements the body.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any


def generate(manifest: Any, out_dir: Path) -> list[Path]:
    raise NotImplementedError("dbus-signals target — Phase 4")
