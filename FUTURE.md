# FUTURE — active-component epics (exploratory, not yet scheduled)

Three larger features that turn `zwaved` from a *reactive* controller (decode
reports, send commands a client asked for) into one that hosts **active
components** on the Z-Wave network:

1. **Addressable presence** — a way for real devices to associate to and address
   the daemon. *Not* necessarily synthetic nodes: the cheapest form is just the
   controller's own node (see E1's tiers).
2. **Scene controller** — real buttons/switches drive daemon-side scenes by
   targeting the daemon's presence.
3. **Thermostat management** — a logical thermostat that aggregates/coordinates
   real climate devices.

**E1 (addressable presence) is the foundation for the other two** — but for the
common cases it's lighter than "virtual nodes": the controller node already
receives frames addressed to it, so Tier 1 needs no new protocol primitives.

These are the write-side / closed-loop cousins of the existing
[closed-loop automation epic (#101)](https://github.com/Assar63/zwaved/issues/101):
that epic reacts to events by driving *real* nodes; these epics give the daemon
its own *addressable presence* so other devices can drive **it**.

```
                ┌────────────────────────────┐
                │ Addressable presence (E1)  │  ← foundation
                │ T1 ctrl-node · T2 endpoints │
                │ · T3 virtual slave nodes    │
                └────────────┬───────────────┘
                ┌────────────┴─────────────┐
        ┌───────┴────────┐      ┌──────────┴─────────┐
        │ Scene ctrl (E2)│      │ Thermostat mgmt(E3)│
        └────────────────┘      └────────────────────┘
```

---

## The core design question: how does a real device address the daemon?

Everything here hinges on giving the daemon an **addressable presence** that real
devices can associate to and send commands to. There are three ways to get that,
in increasing cost and decreasing portability. The original framing assumed the
most exotic one (virtual slave nodes) was mandatory; it isn't.

| Tier | Mechanism | Inbound path | Works on our Static Controller? | Cost |
|------|-----------|--------------|--------------------------------|------|
| **1** | Associate real devices to the **controller's own node ID** | `APPLICATION_COMMAND_HANDLER` (0x04) — already decoded | ✅ **yes, today** | low |
| **2** | **Multi Channel endpoints** on the controller node (CC 0x60 / 0x8E) | same 0x04 path, multi-channel-encapsulated | ✅ likely (needs device-compat spike) | medium |
| **3** | **Virtual slave nodes** (Bridge Controller library) | `APPLICATION_SLAVE_COMMAND_HANDLER` (Bridge-only) | ❌ **no** (see gate result) | high |

**Key insight:** the controller's *own* node already receives frames addressed to
it (that's how the lifeline association and Central Scene #22 work — the daemon
decodes them via `APPLICATION_COMMAND_HANDLER` today). So a real button can
already drive the daemon **without virtual nodes or endpoints at all** (Tier 1).
Endpoints (Tier 2) only matter when one controller node must present *several
distinct targets*. Virtual slave nodes (Tier 3) are the last resort and are the
only tier that needs a Bridge Controller.

> **Dongle library-type probe (2026-06-05):** `scripts/zw-dongle-probe` against
> `/dev/ttyACM0` (Aeotec Z-Stick Gen5, `0658:0200`) →
> `version = "Z-Wave 3.99"`, `libraryType = 1 (Static Controller)`.
> **Tier 3 is therefore unavailable on this hardware** — but Tiers 1 and 2 are
> not, so the foundation is *not* blocked. Re-run the probe on any candidate
> dongle if Tier 3 ever becomes necessary. (The daemon also now logs/exposes the
> library type by name — #118.)

---

## Epic 1 — Addressable presence (foundation)

**Goal:** give the daemon a presence on the network that real devices can
associate to and send Command Class frames to, and that the daemon decodes into
typed events for the orchestrators (E2/E3) to act on. Pursue the cheapest tier
that satisfies the need; only climb when a real requirement forces it.

### Tier 1 — associate to the controller node (do this first; works today)

The controller already has a node ID and already receives frames addressed to it
via `APPLICATION_COMMAND_HANDLER` (0x04) → `MessageBus::ApplicationCommand`
(decoded today; Central Scene #22 already rides this path). So:

- **No new host-API work.** A real device is configured (its own association
  group) to target the controller's node ID — the existing `SetAssociation`
  flow and auto-lifeline already do exactly this kind of grouping.
- The daemon keys behaviour on `(sourceNodeId, decoded CC)` — e.g. "node 7 sent
  Central Scene scene 1 → run scene A". This is literally the closed-loop
  automation epic (#101) consuming an event it can already see.
- **Limitation:** one target (the controller node). Multiple logical functions
  must be distinguished by `(sourceNode, command/scene)`, which is enough for
  most "button → scene" cases but not for "one device, several association
  groups each meaning a different daemon target."

This tier needs *zero* new protocol primitives — it's an orchestrator + a store.
**E2 (scene controller) should be built on Tier 1.**

### Tier 2 — Multi Channel endpoints on the controller (when Tier 1's single target isn't enough)

Present several addressable **endpoints** on the controller's own node using
Multi Channel (CC 0x60) + Multi Channel Association (CC 0x8E), so one real device
can multi-channel-associate distinct groups to distinct daemon endpoints.

- Inbound rides the **same `APPLICATION_COMMAND_HANDLER` path** (multi-channel-
  encapsulated), so **no Bridge Controller required** — this is the key advantage
  over Tier 3 and the reason it's worth a spike.
- **New role for the daemon:** today zwaved is purely a controller/*initiator*;
  it never *responds* to application-CC queries. Tier 2 needs the daemon to act
  as a **Multi Channel responder** — answer Endpoint/Capability Get, decode
  multi-channel encapsulation — which is genuinely new infrastructure (a
  `src/endpoint/` responder + Multi Channel encap/decap codecs).
- **Unknown to de-risk first:** whether real devices will multi-channel-associate
  to a *controller's* endpoints at all (non-standard; device-dependent). **Spike
  before committing.**

### Tier 3 — virtual slave nodes (last resort; needs a Bridge Controller)

The original idea: the daemon hosts real-looking virtual *slave* nodes via the
Bridge-Controller-only Serial API family (`SET_SLAVE_LEARN_MODE`,
`SEND_SLAVE_NODE_INFORMATION`, `APPLICATION_SLAVE_COMMAND_HANDLER`,
`SEND_SLAVE_DATA`, `GET_VIRTUAL_NODES`, `IS_VIRTUAL_NODE` — verify exact FUNC_IDs
against `docs/`). Each virtual node is a fully independent network identity.

- **Blocked on current hardware** (Static Controller — see the probe result
  above). Requires a Bridge-Controller dongle.
- Only pursue if Tiers 1–2 prove insufficient *and* a Bridge Controller is
  available. Would add `src/virtual-node/VirtualNodeRegistry` (SQLite,
  `(home_id, virtual_node_id)`) + slave-FUNC_ID host-API codecs + a
  `VirtualNodeCommand` bus event, mirroring the existing `AddNode`/`SendData`
  request/callback machinery.

### Phased plan
1. **Tier 1** — scene/desired-state orchestrator + store on the existing
   `ApplicationCommand` path (this *is* most of E2/#101; no protocol work).
2. **Tier 2 spike** — can a real device multi-channel-associate to a controller
   endpoint? If yes: Multi Channel encap/decap codecs + a responder module.
3. **Tier 3** — only if a Bridge Controller is on hand and Tiers 1–2 fall short.

**Cross-tier unknowns:** which CC real controllers emit to an association target
(Basic Set vs Central Scene vs Scene Activation — drives what the daemon must
decode/advertise); security (responding under S2 is a large follow-up — start
non-secure / S0).

---

## Epic 2 — Scene controller (build on E1 Tier 1; works on current hardware)

**Goal:** physical controls (wall switches, remotes) drive **daemon-side
scenes**: real device's association group → the controller node → daemon runs a
stored scene (a set of SendData actions against real nodes).

**Build on:**
- **E1 Tier 1** — the real device's association group targets the controller's
  own node (the existing `SetAssociation` / auto-lifeline flow); its presses
  arrive as `MessageBus::ApplicationCommand` and are typed by the cc-translator
  (Central Scene #22 already lands here). *No virtual node, no endpoints, no new
  protocol primitive.*
- A **scene store** (SQLite, `nodes.db`): a *trigger* table
  `(home_id, sourceNodeId, sceneNumber, keyAttribute) → scene_id` plus a
  *scene* table `scene_id → [ (targetNodeId, ccPayload) ]` ordered actions.
  Reuses the `PendingQueue` / `PolicyRegister` storage idiom.
- A `SceneOrchestrator` (`src/orchestrator/`, prio 204, bus-only): subscribes to
  the typed press event, looks up the trigger key, and replays the scene's
  actions as `SendDataCommand`s — exactly the closed-loop pattern from #101.

**Context-dependent scenes are a Tier-1 native feature, not a reason for
endpoints.** Every inbound frame carries the **source node**, so the *same*
scene number from *different* senders maps to different scenes for free:

```
(node 7  "living room", scene 1, press 1x) → "TV mode"
(node 12 "hallway",     scene 1, press 1x) → "Good bye, see you after work"
```

i.e. the key is `(sourceNodeId, sceneNumber, keyAttribute)`. Room names for the
UI come from node metadata (#83). Endpoints (Tier 2) disambiguate by **target**,
not source — they only add value when a **single** device must mean several
different things but emits the **same generic command** (e.g. plain `Basic Set`)
on each of its association groups, so the payload alone can't tell them apart.
Central Scene controllers avoid even that by putting `sceneNumber` +
`keyAttribute` in the frame — so for scene remotes, Tier 1 is sufficient.

**D-Bus:** scene CRUD (`SetScene` / `GetScene` / `DeleteScene` / `ListScenes`)
plus trigger CRUD (bind a `(sourceNode, scene, key)` to a scene_id) and a
`SceneActivated` signal for observability.

**Open questions:** which CC real wall controllers emit to an association target
(Basic Set vs Scene Activation vs Central Scene) — drives what the orchestrator
keys on; only non-scene devices that send the same generic command on multiple
groups would push toward E1 Tier 2 endpoints.

---

## Epic 3 — Thermostat management (mostly E1-independent)

**Goal:** a logical thermostat (Thermostat Mode / Setpoint / Operating State /
Fan Mode — the quartet we already decode, #23) that aggregates and coordinates
one or more real climate devices: a setpoint written to the logical thermostat
fans out to the real TRVs/HVAC; real sensor readings feed back its state.

**Key realisation:** if the logical thermostat is something *clients* read/write
over D-Bus and the daemon *drives* real devices from it, it needs **no E1
presence at all** — it's pure orchestration. E1 only matters if a *real* device
must associate to the logical thermostat (rare); defer that to a Tier-1/2 add-on.

**Build on:**
- A D-Bus "logical thermostat" object (its own methods/signals) — no virtual
  node required for the daemon-driven direction.
- A `ThermostatOrchestrator` mapping logical-thermostat Sets → real-device Sets
  and real-device Reports → logical state, with a desired-state store (overlaps
  strongly with the #101 "desired-state PolicyRegister entries" child — likely
  the same mechanism).
- Closed-loop control (setpoint vs measured temperature) is the genuinely hard
  part and overlaps with #101's verify-after-set / event-triggered work.

**Open questions:** how much logic is "aggregation/mirroring" (cheap) vs
"control loop" (hard — schedule/PID-ish, probably its own epic); relationship to
room/house metadata (#83) for grouping which real devices a logical thermostat
governs.

---

## Cross-cutting notes

- **Reuse, don't reinvent:** the orchestrator pattern (bus-only, prio 204), the
  SQLite `(home_id, …)`-keyed store idiom, the cc-translator for typed decode,
  and the manifest-driven codegen all extend cleanly to these. Tier 1 needs *no*
  new protocol primitive at all; Tier 2 adds a Multi Channel *responder* role
  (new); Tier 3 adds the virtual/slave host-API layer (new + Bridge Controller).
- **Relationship to #101:** E2 (Tier 1) and E3 are concrete instances of the
  closed-loop automation #101 raised; the desired-state store and
  verify-after-set children there should probably be built *with* E3 rather than
  separately.
- **Security:** responding under S2 (Tier 2/3) is a large follow-up — start
  non-secure / S0.

## Suggested sequencing

1. **E2 on E1 Tier 1** — scene orchestrator + scene store on the existing
   `ApplicationCommand` path. Works on the current Static Controller; no protocol
   work; proves the closed loop end-to-end. Highest value, lowest cost.
2. **E3 (daemon-driven)** — logical-thermostat orchestrator + desired-state store
   (with #101); no E1 presence needed for the daemon-drives-devices direction.
3. **E1 Tier 2 spike** — only if a setup needs multiple distinct targets per
   controller node: test whether real devices multi-channel-associate to a
   controller endpoint, then build the responder if it works.
4. **E1 Tier 3** — only if Tier 2 is insufficient/incompatible *and* a Bridge
   Controller dongle is available.

## Before filing as GitHub epics

- [x] Resolve the Bridge Controller question — **done 2026-06-05: current
      Z-Stick Gen5 is a Static Controller (libtype 1). This only rules out
      E1 Tier 3; Tiers 1 (E2) and 2 are unaffected, so the foundation is not
      blocked.**
- [x] Confirm which CC real wall controllers emit to an association target (E2 /
      Tier 1) — **resolved by implementing all three sources (#124): Central
      Scene, Basic Set, and Scene Activation, behind a `source` discriminator,
      so no single-CC bet was needed.**
- [x] Decide whether `(sourceNode, command)` keying (Tier 1) is sufficient or
      whether any target setup forces the Tier 2 endpoint spike — **Tier 1
      keying shipped and sufficient for E2; Tier 2 remains an unscheduled E1
      spike.**
- [x] Decide whether E3's control loop is in-scope or a separate epic — **decided
      2026-06-09: out of scope for epic #131 (aggregation/mirroring only); the
      closed-loop control is deferred to its own future epic.**
- [x] Then split E2 / E3 into per-PR child issues (orchestrator, store, D-Bus,
      terminal) like the thermostat quartet (#23) — **E2 = #119 (#120–#124, all
      merged); E3 = #131 (#132–#135). E1 Tier 2/3 not yet split.**

## Status / filed issues

- **E2 (scene controller) is complete** — epic **#119** with all children
  merged: **#120** (scene store) ✅, **#121** (SceneOrchestrator) ✅, **#122**
  (D-Bus CRUD) ✅, **#123** (terminal UI) ✅, **#124** (extra trigger sources:
  Basic Set 0x20 + Scene Activation 0x2B, behind a `source` discriminator) ✅.
  Scenes run end-to-end from Central Scene, Basic Set, or Scene Activation
  presses (see MANUAL §16d).
- **E3 (thermostat management) is in progress** as epic **#131**, scoped to
  aggregation/mirroring (the closed-loop control deferred to its own epic) and
  grouping via node metadata (#83) rather than a new store. Children: **#132**
  (NodeMetadata reverse lookup / nodes-by-tag) ✅, **#133** (ThermostatOrchestrator
  fan-out + mirror) ✅, **#134** (D-Bus logical-thermostat surface), **#135**
  (terminal UI). E1 remains exploratory here.
- Old TODO.md stubs are superseded by this doc:
  - "Virtual nodes" (#29) → **E1** (addressable presence).
  - "Scene controller thread" (#30) → **E2 / epic #119** (closed in favour of
    #119, delivered).
  - "Thermostat management" → **E3**.

---

## Longer-horizon / unlikely (moved from TODO.md)

Tracked for completeness; each is niche, very large, or both, with no concrete
bench device or use case demanding it today. Promote into TODO.md if a real need
shows up. Each already has a GitHub issue.

- [ ] [OTA firmware update of nodes (Firmware Update Meta Data CC 0x7A)](https://github.com/Assar63/zwaved/issues/49)
- [ ] [OTW firmware update of the dongle itself](https://github.com/Assar63/zwaved/issues/50)
- [ ] [800-series silicon + Z-Wave Long Range](https://github.com/Assar63/zwaved/issues/51)
- [ ] [Audit log of administrative D-Bus calls](https://github.com/Assar63/zwaved/issues/52)
- [ ] [Metrics / Prometheus exporter](https://github.com/Assar63/zwaved/issues/53)
- [ ] [Fuzzing the frame parser and ApplicationCommand decoder](https://github.com/Assar63/zwaved/issues/54)
