"""
targets.dbus_methods — generate DBusMethods.gen.cpp.

Inputs (from InterfaceManifest.yml):
    dbus.bus_name / object_path / interface
    dbus.methods:              every method declared on the daemon's
                               D-Bus surface, with one of four `action:`
                               shapes:
        publish: <Event>
            forward inbound D-Bus args to a MessageBus command event
            of the named type (default field mapping is by name)
        publish_constant: { event: <E>, fields: {<f>: <value>} }
            same as `publish:`, but with named fields fixed to
            constants (e.g. mode = MODE_STOP)
        read_cached: <StateEvent>
            return the latest retained value of the named state event
        custom: <handlerName>
            emit only a forward declaration; the daemon implements
            <handlerName> by hand (today: GetVersion, GetNetworkStatus)

Outputs (under <out>/):
    DBusMethods.gen.cpp
        - one `obj.registerMethod(...)` invocation per method, with
          the lambda body generated from the action shape
        - flag-byte unpacking for AddNode / RemoveNode (the bool
          fields `power` / `nwi` / `nwe` / etc.) handled as a
          special case described in the manifest's per-method `doc:`

Replaces (after Phase 3 lands):
    - the long flat `obj.registerMethod(...)` block in
      src/external-api/DBusBackend.cpp inside DBusBackend::run()
      (the section the existing
      NOLINTBEGIN(readability-function-cognitive-complexity) wraps)

Template: scripts/codegen/templates/dbus_methods_cpp.j2

This file is a stub. Phase 3 of the rollout implements the body.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any


def generate(manifest: Any, out_dir: Path) -> list[Path]:
    raise NotImplementedError("dbus-methods target — Phase 3")
