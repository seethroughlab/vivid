# ADR-0053: Audio Reactivity Is Authored as Explicit Graph Nodes

Status: accepted (implemented — #288, Phase A + B; 2026-08-08)

Date: 2026-08-07

## Context

Audio→visual reactivity in Vivid is real and capable, but **invisible**. A wire like
`"master.low" → "node:5.scale_x"` makes the bass breathe a sphere, yet nothing about that coupling
appears on the visual node graph. It lives in a separate, string-keyed `MappingRegistry`
(`app/src/mapping.h`, owned by `NodeGraph`), evaluated off to the side each frame. Looking at the
graph, a user cannot tell what is driving a parameter, or that anything is driving it at all.

This came up directly while trying to make the demos genuinely good: "shouldn't there be a node
coming from the audio graph hooked up to the sphere's params?" There isn't. The hidden bridge is a
black box that makes reactive graphs hard to read, hard to author, and hard for agents/tools to plan
over.

This is precisely the "hidden bus" that **ADR-0047 (First-Class Typed Ports)** committed to
eliminating: *"Explicit stream adapters. Cross-domain bridges become nodes… Hidden buses remain
implementation details only where latency or platform constraints require them,"* with *"edge-owned
shaping."* The audio graph already works this way — an `LFO` node's `control_signal` wires into a
device parameter via `EdgeKind::Control`, with depth/curve/polarity carried on the edge
(`app/src/audio/audio_graph.h`). The **visual** graph has the opposite gap: parameters are the one
thing you cannot wire. There is no general value-source that can feed, say, the camera params.

## Decision

Make audio→visual reactivity **explicit in the visual graph**. Audio sources become visible nodes —
a `Master` node and one node per `Track` — whose named scalar outputs (level / transient / low /
mid / high, plus transport phases; and per-track note / velocity / gate) are wired by real edges
into visual operator **parameters**. Modulation shaping (amount, curve, polarity, output range,
attack, release) is **edge-owned**, and the manually-authored base value is never baked (ADR-0022).
The hidden string mapping is absorbed into graph-resident control edges; the string API remains as
sugar over the graph for back-compat and agents.

This completes ADR-0047's direction for the audio→visual bridge and unifies visual modulation with
the audio graph's control-edge model. As a consequence, *any* value-producing node — not just audio —
can drive a parameter, unlocking LFO/envelope/value sources into camera and shape params.

Deliver it **phased**, additive and non-breaking at every step:

- **Phase A — visible source nodes.** Audio sources render as multi-output source nodes on the
  visual canvas, wired to params. The existing `MappingRegistry` remains the resolution engine, so
  the change is confined to the UI/bridge layer and touches none of the operator ABI, the visual
  `run_chain`, or the known crash surfaces. Every mapping — including ones created programmatically —
  becomes visible.
- **Phase B — typed control edges.** Visual op parameters become first-class wireable targets;
  `Master`/`Track` become true `VisualGraph` source ops emitting value lanes (the proven
  `AudioSpectrum` shape — a `SOURCE` op reading a host analysis bus and committing a
  `SCALAR`/`MANY` lane, which `run_chain` already resolves); the string registry is absorbed into
  graph control edges. Old projects migrate transparently at load. `MappingRegistry` runs in
  parallel until a final cutover, after which it is relegated to the reverse visual→audio path only.

## Alternatives Considered

- **Draw source nodes as a skin over the hidden registry (no convergence).** Fastest to ship the
  look and reuses all existing drawing/persistence. Rejected as the end state: it entrenches two
  parallel modulation systems — the precise opacity ADR-0047 exists to remove. (It survives, however,
  as Phase A: a safe, useful stepping stone whose data model bridges cleanly to Phase B.)
- **A fully generic node-based modulation type system up front.** Deferred. The immediate need is
  audio sources + wireable params with edge-owned shaping, mirroring the audio graph's proven
  control-edge model. A more general value-routing story can grow from that seam.
- **Keep the bridge, document the side channel better.** Rejected. Documentation does not make the
  coupling visible or editable in the graph, which is the whole point.

## Consequences

- The graph becomes honest: what drives a parameter is visible and rewireable, for users and agents
  alike. Reactive graphs become far easier to author and to make legible.
- Visual modulation converges with the audio graph's control model (shared shaping struct + curve
  shaper), reducing conceptual surface area.
- Parameters become general wireable targets, unlocking non-audio value sources (LFO/envelope/value
  nodes → camera/shape) as a natural follow-on.
- Cost: Phase B adds a param-control-edge model to `VisualGraph`, a resolution step in `run_chain`,
  a live-analysis host bus + two source ops, a load-time migration, and editor support for
  param-input ports and control edges. It also brings source-node creation onto the live-topology
  path, whose known instability must be avoided via atomic `load_graph` rebuilds for batch creation.
- Back-compat: old projects and the string `connect_mapping` API keep working — mappings migrate to
  edges at load, and the string API becomes sugar over the graph.

## References

- ADR-0047: Note, Control, and Value Streams Need First-Class Ports (this completes it for the
  audio→visual bridge)
- ADR-0041: Procedural 3D Scene Graph for Audio-Reactive Visuals
- ADR-0046: Operators Are Composable Primitives First
- ADR-0022: The Session Audio Graph — base vs resolved parameter model (edges never bake the base)
- Code: `app/src/mapping.h`, `app/src/ui/node_graph.cpp` (`apply_params`), `app/src/gpu/visual_graph.cpp`
  (`run_chain`), `app/src/audio/audio_graph.h` (`EdgeKind::Control` / `ControlShape` / `control_resolve`),
  `app/operators/packages/vivid-3d/audio_spectrum.cpp` + `app/src/operator_api/spectrum_bus.h`
  (source-op-reading-a-host-bus template)
