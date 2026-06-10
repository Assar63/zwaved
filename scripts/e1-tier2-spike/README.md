# E1 Tier 2 spike (#143) — will a real device associate to a *controller's* endpoints?

Scaffold + procedure for the [#143](https://github.com/Assar63/zwaved/issues/143)
de-risking spike, the gate for the rest of E1 Tier 2 (epic
[#142](https://github.com/Assar63/zwaved/issues/142)).

**This is a doc, not a tool.** The spike is fundamentally an on-hardware test;
there is intentionally no responder code here yet (that would be
[#145](https://github.com/Assar63/zwaved/issues/145), the very thing this spike
is meant to gate). What follows decomposes the unknown into ordered
sub-questions, the cheapest first, with explicit go/no-go criteria.

## The unknown

E1 Tier 2 wants the daemon to present **Multi Channel endpoints on its own
(controller) node** so a real device can multi-channel-associate distinct
association groups to distinct daemon endpoints — giving the daemon several
addressable targets without a Bridge Controller (Tier 3) or new slave nodes.

This is **non-standard**: devices normally multi-channel-associate to *slave*
endpoints, not to a controller's. So before building the endpoint responder
(#145) and inbound routing (#146), confirm a real device will actually do it.

## Prerequisites

- The daemon running against the dongle (`./cmake-build-gnu/zwaved`), D-Bus
  policy installed (see MANUAL §1).
- A real device that **supports Multi Channel Association (CC `0x8E`)** —
  check `GetNodes` / the terminal's node list for `0x8E` in its CC list.
- The controller's own node id (from `GetNetworkStatus` / `GetDongleInfo`).
- A second terminal running `busctl --system monitor com.tiunda.ZWaved` (or the
  `zwave-terminal` activity pane) to watch inbound frames.

## Sub-question 1 — does the device *accept* a `(controllerNode, endpoint)` association target? (cheap; no responder needed)

The daemon can already send a Multi Channel Association SET to a device
(`SetMultichannelAssociation`) and read it back (`GetMultichannelAssociation`).
Use that to ask the device to add an **endpoint member pointing at the
controller node**, then read it back and see whether the endpoint survived.

```bash
CTRL=1          # <- the controller's own node id (from GetNetworkStatus)
DEV=7           # <- the CC 0x8E-capable device under test
GROUP=2         # <- a multi-channel association group on the device

# Add endpoint member (controllerNode, endpoint=2) to the device's group.
#   args: nodeId groupId nodeMembers(ay) endpointMembers(a(yy)) callbackId
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved com.tiunda.ZWaved1 \
    SetMultichannelAssociation 'yyaya(yy)y' $DEV $GROUP 0 1 $CTRL 2 0

# Read the group back.
busctl --system call com.tiunda.ZWaved /com/tiunda/ZWaved com.tiunda.ZWaved1 \
    GetMultichannelAssociation yyy $DEV $GROUP 0
# Watch the MultichannelAssociationReport signal for the stored members.
```

**Go / no-go:**
- ✅ The report shows the `(controllerNode, endpoint=2)` pair stored → the device
  accepts controller-endpoint targets. Proceed to SQ2.
- ❌ The endpoint is dropped (stored as a plain `controllerNode` whole-node
  member) or the SET is refused → this device won't target a controller
  endpoint. Tier 2 is unlikely to work *with this device*; try another, else
  record no-go.

## Sub-question 2 — does the device then *send* a Multi-Channel-encapsulated frame to that endpoint?

Trigger the device's group (press its button / trip the sensor) and watch what
arrives.

- The daemon already decodes inbound `APPLICATION_COMMAND_HANDLER` frames into
  `MessageBus::ApplicationCommand` (logged by the cc-translator). With the #144
  codec, a frame whose bytes start `0x60 0x0D …` is a
  `MULTI_CHANNEL_CMD_ENCAP` — `MultiChannel::decodeEncap` yields the
  `destinationEndpoint` it was addressed to.
- Look in the monitor / activity pane for an inbound frame from `DEV` that is
  multi-channel-encapsulated with `destinationEndpoint == 2`.

**Important caveat (the likely finding):** a spec-compliant device may first
query the controller's endpoints (`MULTI_CHANNEL_ENDPOINT_GET` /
`CAPABILITY_GET`) and, getting **no response** (no responder exists yet — that's
#145), either refuse to encapsulate or fall back to addressing the root device
(endpoint 0). So SQ2 may be **inconclusive without #145**. If so, that itself is
the key result: the go/no-go for building #145 rests on **SQ1 + this reasoning**
rather than a clean SQ2 observation.

**Go / no-go:**
- ✅ An encapsulated frame addressed to endpoint 2 arrives → Tier 2 is viable;
  build #145 (responder) + #146 (routing).
- ⚠️ Frames arrive at the root (endpoint 0) / nothing arrives until endpoints are
  advertised → Tier 2 needs the responder before it can be confirmed. Decide
  whether SQ1 alone is encouraging enough to build #145 speculatively, or to
  park Tier 2.
- ❌ Device won't encapsulate at all → Tier 2 not viable with this device.

## Optional observability aid

If the existing logging isn't enough to spot encap frames, a one-line debug log
in the cc-translator (`if (auto e = MultiChannel::decodeEncap(ccData)) Logger::info(...)`)
makes inbound encapsulation visible without building any responder. Add it
behind a temporary debug flag during the spike; remove before merging anything.

## Recording the result

Fill this in and copy the conclusion into the #143 issue **and** FUTURE.md's
E1 Tier 2 paragraph:

| Device (mfr / product) | Supports 0x8E? | SQ1: stores `(ctrl, ep)`? | SQ2: sends encap to ep? | Verdict |
|------------------------|----------------|---------------------------|-------------------------|---------|
|                        |                |                           |                         |         |

**Decision:**
- **GO** → unblocks #144✅ / #145 / #146.
- **NO-GO** → document why; multi-target presence then needs Tier 3 (virtual
  slave nodes, #147) which requires a Bridge Controller dongle.
