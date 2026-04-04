# Composite-Local Modulation Assignment V1/V2

## Summary
Implement `#2` as a **composite-local** modulation assignment layer that builds on Step 1's exposed control surface.

V1 should be:
- available to any composite/module, not just synths,
- centered on named modulation sources and named modulation destinations,
- serialized readably on the composite instance,
- lowered back into ordinary internal graph semantics rather than adding a second routing substrate.

Key boundaries:
- normal wires remain the primary graph routing model,
- this does not introduce a new first-class executable concept called `modulator`,
- `modulator` may be used as UI language for a source role, but not as a new runtime object type.

## Story
Step 1 makes a graph patch feel like one coherent reusable node. Step 2 adds a small local modulation surface on top of that exposed control surface so the node can be shaped quickly without reopening every internal wire.

For `vivid-wavetable`, a module author can expose sources such as `env2`, `lfo1`, `macro1`, and `velocity`, then let a user assign those sources to destinations such as `brightness`, `motion`, `filter_cutoff`, or `wt_position`. The result feels closer to a finished authored module without moving synth logic out of the package.

For a particle-system module, the same pattern applies. The author can expose sources such as `bass_energy`, `onset`, `pointer_x`, and `macro1`, then let a user assign them to destinations such as `emission`, `drift`, `size`, or `hue_shift`. The exact same local modulation surface works for audio, visual, and hybrid modules, preserving audio-visual parity.

## Rationale
### Why this is useful
- Packages already have sophisticated modulation internally, but it is often graph-first and patch-fragile.
- A coherent composite needs a fast way to say "this source should affect this behavior" without reopening the internal graph.
- This improves authorability and playability for audio, visual, and AV composites.

### Why this is not role bindings again
- Role bindings were graph-wide structural references between arbitrary nodes.
- This design is local to one composite instance.
- There is no arbitrary `node_id/output_name` binding model.
- There is no new graph-wide transport abstraction.

### Why this is not embedded operators again
- The feature does not require reviving runtime-polymorphic embedded operator metadata.
- Sources are existing internal or exposed signals.
- Destinations are named internal target bindings.
- The assignment layer is metadata plus lowering, not a new embedded-slot runtime system.

## Normal Workflow
### Synth workflow
- The author builds a normal internal graph.
- The author exposes public controls.
- The author declares named sources such as `env1`, `env2`, `lfo1`, `velocity`, and `macro1`.
- The author declares named destinations such as `filter_cutoff`, `brightness`, `wt_position`, and `motion`.
- The user drops the module into a graph, wires note/gate/velocity inputs, opens the inspector, adds assignments, and tweaks amount, polarity, and curve.

### Particle-system workflow
- The author builds a normal internal graph.
- The author exposes public controls.
- The author declares named sources such as `bass_energy`, `onset`, `pointer_x`, `burst_env`, and `macro1`.
- The author declares named destinations such as `emission`, `drift`, `size`, `hue_shift`, and `turbulence`.
- The user drops the module into a graph, wires analysis and pointer inputs, assigns sources in the inspector, and tunes them during performance.

## V1 Key Changes
- Add module/composite metadata for:
  - `mod_sources[]`
  - `mod_destinations[]`
- Each source declares:
  - stable name,
  - description,
  - scalar vs lane-aware shape,
  - optional polarity default or semantic hints,
  - binding to an internal signal or exposed control/performance input.
- Each destination declares:
  - stable name,
  - description,
  - scalar vs lane-aware shape,
  - optional semantic hints or display grouping,
  - binding to one internal modulation target or target bundle.
- Add instance-local `mod_assignments[]` storage on module nodes.
- Add inspector UI for:
  - browsing named sources,
  - browsing named destinations,
  - creating and removing assignments,
  - editing amount,
  - editing unipolar or bipolar polarity,
  - optional simple curve selection.
- Lower assignments into ordinary internal graph behavior during module expansion/build rather than inventing a new runtime graph relation.

## Public Interfaces / Schema
- Extend `.vivid-module.json` with:
  - `module.mod_sources`
  - `module.mod_destinations`
- Extend authored graph/module-instance state with:
  - `mod_assignments`
- Assignment entries include:
  - `source`
  - `destination`
  - `amount`
  - `polarity`
  - optional `curve`
- Keep the JSON readable and flat.
- Assignments are part of the composite instance's authored state, not hidden runtime-only UI state.
- No graph-wide assignment objects are introduced.

## Lane-Aware / Per-Note Behavior
- scalar source -> scalar destination: allowed
- scalar source -> lane-aware destination: allowed via broadcast
- lane-aware source -> lane-aware destination: allowed only when lane provenance is aligned or already structurally valid within the composite
- lane-aware source -> scalar destination: disallowed in v1; use an explicit reduction operator inside the graph if needed

Per-note and per-lane modulation should reuse Vivid's existing lane semantics, not invent a separate per-note model. Destinations that want per-note modulation must bind to lane-aware internal targets, not generic scalar params.

## V1 Non-Goals
- No graph-wide modulation matrix
- No arbitrary references to external graph nodes as assignment sources
- No new executable runtime primitive called `modulator`
- No revival of role binding descriptors or bind/rebind/reference bookkeeping
- No revival of runtime-polymorphic embedded-operator metadata
- No lane-aware source to scalar destination implicit reductions
- No attempt to solve macro pages or expressive performance plumbing beyond what is needed for local modulation assignment

## V2 Follow-Up
- richer curve and scaling options
- destination bundles with author-defined weighting behavior
- better visual overlays for assignment visibility in the graph
- optional assignment gestures such as "select source, click knob"
- broader lane-aware authoring helpers
- possible embedded-graph authoring support if Step 1 v2 lands first

## Test Plan
- parsing valid and invalid module modulation metadata
- instance assignment serialization, load, and save
- inspector and query surfaces exposing sources, destinations, and assignments
- assignment lowering producing correct internal target behavior
- scalar broadcast to lane-aware destinations
- aligned lane-aware source to lane-aware destination behavior
- explicit rejection of invalid lane-aware source -> scalar destination assignments
- regression coverage proving normal wires, presets, and ordinary module behavior still work unchanged

## Assumptions And Defaults
- domain-neutral feature, not instrument-specific
- composite-local only in v1
- normal graph wires remain primary
- `modulator` is UI vocabulary only, not a new runtime entity
- additive assignment model only in v1
- one simple curve field at most in v1
- lane semantics reuse existing `lane_set_id` and `lane_id` rules
- graph-wide or general matrix behavior is deferred unless local use proves the pattern
