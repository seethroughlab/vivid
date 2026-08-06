# ADR-0047: Note, Control, and Value Streams Need First-Class Ports

Status: accepted (implemented — #233–#236, #267–#268)

Date: 2026-08-02

## Context

Vivid's graph already has more signal kinds than its operator descriptors can honestly express.

The audio graph has first-class edge kinds internally: audio, note, and control. Native modulators such as
`LFO` and `ADSR` emit control buffers. `Arp` consumes notes and emits notes. The note generators emit notes
while producing no sound. But the operator descriptor surface still presents several of these nodes through
fake or overloaded audio ports. For example, `Arp`, `LFO`, `ADSR`, `Euclid`, `Chord`, and `RandMelody`
declare an audio-buffer output even when the meaningful stream leaves through `note_out` or `control_out`.

The visual graph has a related problem in a better-developed form. It does expose scalar lanes and custom
references (`VividSignal`, `InstanceArray3D`, `VividSceneFragment`), and operators such as `AudioSpectrum`
and `InstancesFromLanes` benefit from that explicitness. But the type system is still uneven across audio,
visual, note, control, lane, and custom-ref domains.

This hurts composability in three ways:

1. The catalog cannot describe what a node really produces or consumes.
2. Agents and UI tools cannot reliably plan valid graph rewrites from descriptors alone.
3. Operators compensate by bundling behavior internally, because the graph cannot represent the smaller
   reusable streams cleanly.

## Decision

Evolve the operator ABI and graph descriptors toward **first-class typed ports for every composable stream**:

- `audio_buffer`
- `note_stream`
- `control_signal`
- `scalar`
- `lane_array`
- `texture`
- `signal`
- `scene_fragment`
- `instance_array`
- `mesh`
- future custom reference types

The descriptor must name the real stream, direction, multiplicity, semantic shape, and transport behavior.
No operator should need to declare a fake audio port solely to appear in a picker or participate in graph
execution.

This is an additive ABI direction, not a breaking rewrite. Existing operators continue to load. Built-ins
can gain richer descriptors first, and the host can keep compatibility shims for older ABI versions.

## Design Direction

1. **Descriptor truth.** `LFO` has a `control_signal` output. `ADSR` has note/gate input behavior and a
   `control_signal` output. `Arp` has `note_stream` input and output. Note generators have `note_stream`
   output. They may also expose silence only if there is a real audio reason.

2. **Typed connection validation.** Graph editors, MCP tools, and persistence validate connections by
   stream type rather than by domain-specific exceptions.

3. **Explicit stream adapters.** Cross-domain bridges become nodes: note-to-signal, audio-to-spectrum lane,
   lane-to-instance, signal-event-to-gate, control-to-lane, and so on. Hidden buses remain implementation
   details only where latency or platform constraints require them.

4. **Edge-owned shaping stays edge-owned.** Depth, polarity, remap curves, clamp behavior, and per-target
   modulation ranges stay on control/value edges or small transform nodes, preserving the ADR-0022 base vs
   resolved param model.

5. **Recipes compile from typed subgraphs.** A recipe can still create a one-click arpeggiated chord stack
   or audio-reactive particle scene, but the saved structure should be a visible typed graph whenever
   feasible.

## Migration

1. **Inventory current lies.** Add an advisory check that flags operators whose declared audio ports are
   only classification shims for note/control/generator behavior.

2. **Add descriptor fields.** Introduce additive fields for real stream type and role while preserving the
   existing `VividPortDescriptor` layout for older packages.

3. **Teach catalog and MCP first.** `list_operators` and `list_audio_operators` should expose real port
   types before the whole UI depends on them. This lets agents start planning with accurate data.

4. **Update built-ins.** Convert `LFO`, `ADSR`, `Arp`, `Euclid`, `Chord`, and `RandMelody` descriptors to
   the richer model with compatibility aliases where needed.

5. **Unify chooser behavior.** Picker categories should derive from real stream roles, not from the
   presence or absence of an audio input.

6. **Retire fake-port assumptions.** Once built-ins and package docs have moved, remove special-case
   classification tables where the descriptor can answer the question directly.

## Alternatives Considered

- **Keep fake audio ports and document the side channels.** Rejected. It preserves behavior but leaves the
  graph opaque to users, agents, validation, and future tooling.

- **Make note/control ports audio-only implementation details.** Rejected. Notes and controls are creative
  materials in Vivid, not private DSP plumbing; they should be visible and rewired.

- **Jump straight to a fully generic dynamic type system.** Deferred. The immediate need is a small set of
  named stream kinds with compatibility shims. A more general custom-type registry can grow from that.

## Consequences

- Operator descriptors become more honest and more useful.
- Some UI code that currently infers role from audio input/output shape will need to read explicit roles.
- MCP and generated reference docs become better planning surfaces for agents.
- This unlocks the operator split proposed in ADR-0046: smaller note, control, signal, lane, and instance
  primitives are only pleasant to use when their ports are explicit.

## References

- ADR-0015: Notes in the Audio Graph
- ADR-0022: The Session Audio Graph — One Rewireable DAG for the Whole Session
- ADR-0034: Modulation Reaches Plugin Params
- ADR-0046: Operators Are Composable Primitives First
- Code examples: `app/src/audio/builtin_audio_ops.cpp`,
  `app/src/audio/audio_graph.h`,
  `app/operators/packages/vivid-3d/audio_spectrum.cpp`,
  `app/operators/packages/vivid-3d/instances_from_lanes.cpp`
