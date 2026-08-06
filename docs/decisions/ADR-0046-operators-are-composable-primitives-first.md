# ADR-0046: Operators Are Composable Primitives First

Status: accepted (implemented — #237–#239, #269–#271)

Date: 2026-08-02

## Context

Vivid's operator catalog is drifting in two directions at once.

The best newer operators are small, graph-friendly pieces. `AudioSpectrum` reads the master spectrum and
emits a lane. `LaneRamp` emits positions. `LanePalette` emits color lanes. `InstancesFromLanes` packs
attribute lanes into an `InstanceArray3D`. `Instancer3D` attaches those instances to a scene fragment.
Each node does one reusable job, so artists and agents can recombine them into many different patches.

Some older or convenience operators collapse a whole workflow into one node. `Instancer`, `Emitter`, and
`Solids` take a `VividSignal` and directly decide lifecycle, layout, color mapping, geometry, rendering,
and compositing. `InstancesFromSignal` is better because it emits `InstanceArray3D`, but it still combines
signal aging, fired-event kicks, layout, palette, position framing, orientation, and scaling in one
adapter. The built-in note generators similarly combine timing, note material, gate, voice management, and
transport behavior in one operator.

These operators are useful. They create a fast first result, and several are good examples. The problem is
that they teach a single intended path: "drop this node to get this look/sound." That cuts against Vivid's
core premise that an operator should be a remixable material, not a pre-defined workflow.

## Decision

Adopt a catalog principle: **first-class operators are composable primitives by default; bundled recipes are
allowed, but they are labeled and ranked as recipes.**

Every new operator must fit one primary role:

1. **Source** — introduces one reusable signal, texture, audio stream, note stream, scene fragment, or lane.
2. **Transform** — changes one stream into the same kind of stream.
3. **Adapter** — converts between two explicit stream types, with minimal interpretation.
4. **Renderer / Sink** — turns graph data into pixels, audio, export, or an output surface.
5. **Recipe** — a convenience node or saved graph pattern that intentionally combines several primitives.

Primitive, transform, adapter, and renderer operators are the preferred catalog surface. Recipe operators
may ship when they help onboarding or preserve compatibility, but they must be treated as shortcuts over
decomposable graph patterns, not as the canonical way to build.

### Composition Rules

- Do not combine acquisition, analysis, mapping, layout, material/color, simulation, rendering, and output
  in one first-class primitive unless the combination is the irreducible algorithm.
- Prefer value outputs over hidden side effects. If an operator computes useful intermediate structure,
  expose it as a lane, signal, instance bundle, scene fragment, note stream, or control stream.
- Put range/depth/polarity/scale decisions on edges or small transform nodes when the same source should
  drive multiple destinations differently.
- Keep source-specific language out of generic operators. A `VividSignal` consumer should describe `pos`,
  `amp`, `active`, and `fired`, not only "pitch", "velocity", "chords", or "arps".
- Separate layout from rendering. A layout generator should not choose mesh/material/rendering; a renderer
  should not know why its instances exist.
- Separate timing from musical material where possible. A rhythm/gate source should be usable with
  arbitrary pitch, chord, sampler, or visual-event consumers.

## Migration

1. **Catalog metadata.** Add a `role` classification to generated operator reference data:
   `source`, `transform`, `adapter`, `renderer`, `sink`, or `recipe`. Until the ABI has a formal field,
   this can be maintained in the reference generator or in curated docs metadata.

2. **Chooser ranking.** Prefer primitives and adapters in the add-node chooser. Recipes remain searchable,
   but they should not crowd out the building blocks.

3. **Demote workflow-shaped visual nodes to recipes.** Treat `Instancer`, `Emitter`, and `Solids` as
   compatibility/onboarding recipes. Their docs should say which primitive graph they stand in for.

4. **Split `InstancesFromSignal` over time.** Keep it as a useful adapter today, but make the preferred path:
   `Signal -> SignalLifecycle/EventLanes -> LaneLayout/Palette -> InstancesFromLanes -> Instancer3D`.

5. **Split note generation over time.** Keep `Euclid`, `Chord`, and `RandMelody` as bundled recipes, then
   grow smaller note/control primitives: rhythm/gate sources, pitch/scale sources, chord voicers, note
   emitters, and note transforms.

6. **Make examples prove recombination.** Showcase graphs should visibly reuse the same source through
   multiple transforms and destinations, rather than presenting a single magic node per effect.

## Alternatives Considered

- **Delete the workflow-shaped operators.** Rejected. They are useful, preserve existing projects, and help
  first-run success. The issue is catalog posture, not their existence.

- **Keep all operators equal and rely on docs.** Rejected. Discovery and agent behavior are shaped by
  metadata and chooser ranking. If recipe nodes are first-class with no distinction, they become the
  default workflow.

- **Only enforce this socially in code review.** Rejected. A durable catalog needs visible classifications,
  examples, and eventually an audit check.

## Consequences

- The catalog becomes smaller in spirit even as it keeps convenience nodes: users see primitives first and
  recipes as shortcuts.
- Agents get a clearer planning substrate because operator metadata says whether a node is a building block
  or a bundled pattern.
- Some existing summaries and docs need cleanup where generic operators still say "notes", "chords", or
  "arps" despite accepting generic signals.
- The follow-up implementation can be incremental: metadata and docs first, new primitives second, recipe
  demotion last.

## References

- ADR-0041: Procedural 3D Scene Graph for Audio-Reactive Visuals
- ADR-0042: Operator Audit — a Per-Operator Definition of Done + Audit Harness
- ADR-0022: The Session Audio Graph — One Rewireable DAG for the Whole Session
- Code examples: `app/operators/packages/vivid-3d/audio_spectrum.cpp`,
  `app/operators/packages/vivid-3d/instances_from_lanes.cpp`,
  `app/operators/packages/vivid-3d/instancer3d.cpp`,
  `app/operators/packages/core-visuals/instancer.cpp`,
  `app/operators/packages/core-visuals/emitter.cpp`,
  `app/operators/packages/core-visuals/solids.cpp`,
  `app/src/audio/builtin_audio_ops.cpp`
