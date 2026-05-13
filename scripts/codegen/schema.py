"""
Dataclasses mirroring the structure of InterfaceManifest.yml.

The loader (loader.py) populates these from the parsed YAML; the
target generators (targets/*.py) consume them. Type expressions on
fields stay as strings in Phase 1 — Phase 2 will parse them into a
type AST when the MessageBus generator needs to emit C++ for each.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional, Union


# ---- Reusable building blocks ---------------------------------------

@dataclass
class Field:
    """One field of an event, struct, or DBus parameter struct."""
    name: str
    type: str  # u8, bool, string, bytes, list<X>, array<u8, 4>, struct ref
    default: Optional[str] = None  # Wire-level default expression
    doc: Optional[str] = None


@dataclass
class Constant:
    """A named constant attached to an event or CC."""
    name: str
    value: str  # 0x01, 0xFE, etc.


@dataclass
class Direction:
    """Documentation-only: who publishes / subscribes to a bus event."""
    publishers: list[str] = field(default_factory=list)
    subscribers: list[str] = field(default_factory=list)


# ---- Top-level sections --------------------------------------------

@dataclass
class Struct:
    name: str
    doc: Optional[str] = None
    fields: list[Field] = field(default_factory=list)


@dataclass
class Event:
    name: str
    category: str  # state | config | transient | command
    direction: Optional[Direction] = None
    doc: Optional[str] = None
    fields: list[Field] = field(default_factory=list)
    constants: list[Constant] = field(default_factory=list)

    @property
    def retained(self) -> bool:
        return self.category in ("state", "config")


# ---- D-Bus methods --------------------------------------------------
# Action variants. Each method declares exactly one of these.

@dataclass
class ActionPublish:
    event: str  # MessageBus command event to publish


@dataclass
class ActionPublishConstant:
    event: str
    fields: dict[str, str]  # field name → constant expression


@dataclass
class ActionReadCached:
    event: str  # retained state event whose latest value is returned


@dataclass
class ActionCustom:
    handler: str  # name of hand-written C++ helper


Action = Union[ActionPublish, ActionPublishConstant, ActionReadCached, ActionCustom]


@dataclass
class DBusParam:
    """One inbound parameter or one returned-struct field."""
    name: str
    dbus: str  # y, b, s, ay, a(yy), ...
    cpp: Optional[str] = None  # u8, bool, bytes, list<EndpointMember>, ...
    doc: Optional[str] = None
    source: Optional[str] = None  # only for return-struct fields with computed value


@dataclass
class MethodReturn:
    type: Optional[str] = None        # list<NodeInfo> style single-type return
    dbus: Optional[str] = None        # matching D-Bus signature when type is set
    struct: list[DBusParam] = field(default_factory=list)  # inline struct return


@dataclass
class Method:
    name: str
    params: list[DBusParam] = field(default_factory=list)
    returns: Optional[MethodReturn] = None
    action: Optional[Action] = None
    doc: Optional[str] = None


# ---- D-Bus signals --------------------------------------------------

@dataclass
class SignalDecode:
    """Optional CC-decoder hop for typed-from-CC signals."""
    codec: str           # e.g. "BinarySwitch.decodeReport"
    input: str           # event field to feed into the codec (typically ccData)
    on_none: str = "skip"


@dataclass
class SignalTrigger:
    event: str
    decode: Optional[SignalDecode] = None


@dataclass
class Signal:
    name: str
    params: list[DBusParam]
    triggered_by: SignalTrigger
    map: dict[str, str] = field(default_factory=dict)  # output field → expression
    doc: Optional[str] = None


@dataclass
class DBus:
    bus_name: str
    object_path: str
    interface: str
    methods: list[Method] = field(default_factory=list)
    signals: list[Signal] = field(default_factory=list)


# ---- Protocol modules ----------------------------------------------
# A module is the daemon-side codec for one protocol feature (e.g. a
# Z-Wave Command Class, a Zigbee cluster, a KNX functional block). The
# wire-specific bits live under a nested `wire:` block so the rest of
# the manifest schema stays protocol-agnostic.

@dataclass
class ModuleWire:
    """Per-module wire-format identifiers. The fields here are
    Z-Wave-shaped today (`class_byte` + `prefix`); a second-protocol
    daemon adds its own keys (e.g. `cluster_id`) without disturbing
    the surrounding schema."""
    class_byte: int
    prefix: Optional[str] = None  # e.g. SWITCH_BINARY for BinarySwitch


@dataclass
class CommandWire:
    """Per-command wire-format details. `byte` is the command byte;
    `payload` is the list of C++ expressions that go into the encoder
    body after the (COMMAND_CLASS, command) prefix bytes."""
    byte: int
    payload: Optional[list[str]] = None


@dataclass
class Command:
    name: str
    wire: CommandWire
    # Optional rather than default-empty-list: the codegen needs to
    # distinguish "the YAML didn't declare this command's wire shape"
    # (None — leave the encoder hand-written) from "explicitly empty"
    # (a no-arg encoder like Get).
    encode_args: Optional[list[Field]] = None
    decoded_struct: Optional[list[Field]] = None


@dataclass
class Module:
    name: str
    wire: ModuleWire
    description: Optional[str] = None
    constants: list[Constant] = field(default_factory=list)
    commands: list[Command] = field(default_factory=list)


# ---- Translation rules ---------------------------------------------
# Drive the translator module: subscribe to a raw transient bus event,
# run a codec on a named field, and republish a typed event.

@dataclass
class TranslationDecode:
    codec: str            # e.g. "BinarySwitch.decodeReport"
    input: str            # field name on the trigger event
    on_none: str = "skip"


@dataclass
class Translation:
    publishes: str                  # name of the typed event the rule fills
    triggered_by: str               # name of the raw event subscribed to
    decode: TranslationDecode
    map: dict[str, str] = field(default_factory=dict)  # typed-event field → expr


# ---- Root -----------------------------------------------------------

@dataclass
class Manifest:
    version: int
    structs: list[Struct] = field(default_factory=list)
    events: list[Event] = field(default_factory=list)
    dbus: Optional[DBus] = None
    modules: list[Module] = field(default_factory=list)
    translations: list[Translation] = field(default_factory=list)

    def event_by_name(self, name: str) -> Event:
        for ev in self.events:
            if ev.name == name:
                return ev
        raise KeyError(f"unknown event '{name}'")

    def struct_by_name(self, name: str) -> Struct:
        for st in self.structs:
            if st.name == name:
                return st
        raise KeyError(f"unknown struct '{name}'")
