# Per-Note Expression And Performance Surface V1/V2

## Summary
Implement `#5` as two linked additions built on Step 1 and Step 2, not as a separate instrument runtime:

- a standard expressive-note contract centered on `MidiInput`, lane arrays, and existing `lane_id` semantics,
- a curated module performance page generated from exposed params, so instrument modules can present macro/performance controls without separate state.

V1 should make per-note expression routable, queryable, and package-usable without a graph-schema change and without adding a package-specific synth path.

## Story
Step 1 makes a graph patch feel like one coherent module. Step 2 gives that module a small local modulation surface. Step 5 makes the same module feel playable as an instrument.

For `vivid-wavetable`, that means a module instance can receive note, gate, velocity, per-note bend, pressure, slide, and expression through one stable control contract, while also presenting a small performance surface for macros and live controls. The package still owns the synth voice. Core owns the plumbing and surface conventions that make the voice feel coherent.

For non-synth modules, the same structure should still help. A hybrid AV module can read note-aligned expression lanes for animation depth, filter motion, or spatial behavior while exposing a few performance controls such as `macro1`, `macro2`, `energy`, or `motion`. The feature should stay package-generic, not wavetable-specific.

## Rationale
### Why this is useful
- Vivid already has lane-aware note, velocity, and gate usage patterns, but expressive control is inconsistent and incomplete.
- Modern instruments feel coherent when note data and expressive performance data arrive through one stable convention.
- Module authors should not have to reinvent per-note bend, pressure, and expression wiring in every package.
- A curated performance page is the missing UI counterpart to exposed controls and local modulation.

### Why this should build on current lane semantics
- Vivid already has `VIVID_PORT_LANE_ARRAY`, `lane_set_id`, `lane_id`, and lane-state infrastructure.
- Step 2 already established that per-note modulation should reuse lane semantics instead of creating a separate per-note abstraction.
- Reusing lane semantics keeps control, audio, and hybrid modules aligned with the existing graph model.

### Why this is not a new instrument bus
- The feature does not introduce a graph-wide instrument transport layer.
- It does not require a separate runtime representation for instruments.
- It does not add a package-specific synth signal path.
- It standardizes expressive inputs and performance-surface metadata on top of the existing graph and module system.

## Normal Workflow
### Instrument module workflow
- The author builds a normal module with exposed params and ports.
- The author chooses which exposed params belong on the live performance surface.
- The author binds the module internals to standard expressive note inputs such as note, gate, velocity, bend, pressure, and expression.
- The user drops the module into a graph, wires one expressive source, and gets a coherent inspector surface for macros and performance controls.
- The package uses note-aligned lanes internally without inventing its own MPE contract.

### Hybrid AV workflow
- The author exposes a small set of performance params such as `macro1`, `macro2`, `motion`, and `brightness`.
- The author reads the same expressive note lanes to drive audio timbre and visual response together.
- The user performs through one consistent set of note/expression signals and one small page of live controls.

## V1 Key Changes
- Extend `MidiInput` into the canonical expressive-note source while preserving its current ports and behavior.
- Keep the existing outputs:
  - `note`
  - `velocity`
  - `gate`
  - `trigger`
  - `pitch_bend`
  - `mod_wheel`
  - `cc_value`
  - `notes`
  - `velocities`
  - `gates`
  - `midi_out`
- Add lane-array outputs for:
  - `lane_ids`
  - `pitch_bends`
  - `pressures`
  - `slides`
  - `expressions`
  - `channels`
- Add scalar outputs for:
  - `aftertouch`
  - `expression`
- Add a mode param with compact choices:
  - `poly_shared`
  - `mpe_lower`
  - `mpe_upper`
- In `poly_shared`, shared bend/pressure/expression values broadcast to all active note lanes.
- In MPE modes, member-channel bend, pressure, slide, and expression map onto the active note lane for that channel.
- Keep the v1 lane cap aligned with the current `MidiInput` held-note limit.
- Make lane identity stable enough that downstream `vivid_lane_state(...)` users follow note ownership across reorder and release.

## Public Interfaces / Schema
- Extend the effective expressive-note contract around `MidiInput`; do not introduce a second custom event type for v1.
- Standardize output meanings and ranges:
  - `notes`: MIDI note numbers
  - `velocities`, `gates`, `pressures`, `slides`, `expressions`: `0..1`
  - `pitch_bends`: `-1..1`
  - `channels`: `1..16`
- Extend semantic-tag vocabulary in `docs/SEMANTIC-PARAM-TAGS.md` for expressive-note surfaces:
  - `midi_pitch_bend`
  - `midi_pressure`
  - `midi_expression`
  - `midi_slide`
  - `midi_channel`
- Extend `.vivid-module.json` exposed-param metadata with optional performance-surface fields:
  - `performance_page`
  - `performance_order`
  - `performance_role`
- `performance_role` should stay compact in v1. Suggested built-in values:
  - `macro`
  - `mod_wheel`
  - `expression`
  - `aftertouch`
  - `xy_x`
  - `xy_y`
- Performance controls remain ordinary exposed params:
  - no separate persisted macro state,
  - no separate performance-only serialization path,
  - presets, variations, and Step 2 local modulation continue to operate on these params normally.
- Extend synthetic module `OperatorInfo`, graph snapshots, and control-server query surfaces so they expose performance-page metadata the same way they expose other module param metadata.

## Performance Surface Behavior
- Add a module-level `Performance` inspector tab or section for selected module instances.
- Show only exposed params tagged with `performance_page`.
- Group by `performance_page` and order by `performance_order`.
- Reuse existing exposed-param metadata and controls rather than inventing a second widget model.
- If an exposed param is also used as a Step 2 local modulation source, the same authored control can appear in both roles without duplicate storage.
- No new control-server mutation API is needed in v1. `set_param` remains the write path because performance controls are still params.

## Lane-Aware / Per-Note Behavior
- The expressive-note surface should reuse existing lane semantics end to end.
- `notes`, `velocities`, `gates`, `pitch_bends`, `pressures`, `slides`, `expressions`, and `channels` belong to the same note-aligned lane family.
- `lane_ids` should be published explicitly so downstream audio/control operators can preserve identity-aware state across compaction and note turnover.
- Package operators that consume per-note expression should do so through ordinary lane-array inputs, not through a new side channel.
- Scalar fallbacks remain useful for non-polyphonic modules:
  - `pitch_bend`
  - `aftertouch`
  - `mod_wheel`
  - `expression`
- V1 should prefer one canonical expressive source contract over multiple partially overlapping operator conventions.

## V1 Non-Goals
- No new graph-wide instrument bus
- No separate instrument runtime representation
- No separate persisted macro state outside ordinary param values
- No automatic MPE zone discovery or hardware-profile system
- No full controller-learn matrix for performance pages
- No requirement to retrofit every existing audio operator to consume the new expression lanes
- No new graph schema change requirement for authored graph JSON in v1

## V2 Follow-Up
- richer performance-page widgets or gestures
- tighter integration between performance controls and local modulation assignment UI
- stronger package/browser affordances for performance-ready modules
- broader controller-learn workflows for performance surfaces
- deeper MPE configuration if real package usage proves the need

## Test Plan
- `MidiInput` regression coverage proving current note/velocity/gate behavior remains unchanged
- `poly_shared` mode broadcasting bend, pressure, and expression across active note lanes
- `mpe_lower` and `mpe_upper` mapping per-channel expressive data onto the correct active lanes
- stable `lane_ids` across note reorder, release, and compaction
- `midi_out` passthrough remaining intact
- module metadata parsing for valid and invalid `performance_page` / `performance_role` fields
- synthetic operator and query surfaces exposing performance metadata and new expressive-port metadata
- inspector rendering for a module with a `Performance` page
- regression coverage proving presets, variations, exposed controls, and Step 2 modulation still work with performance-tagged params because they are still ordinary params

## Assumptions And Defaults
- Chosen default: extend `MidiInput` rather than add a separate per-note-expression source operator
- Chosen default: performance pages are curated views over exposed params, not a new state model
- Chosen default: v1 supports `poly_shared`, `mpe_lower`, and `mpe_upper`; broader controller/MPE configuration is deferred
- Chosen default: expressive-note plumbing reuses lane arrays, `lane_set_id`, and `lane_id` rather than introducing a second per-note transport abstraction
- Existing graph routing remains central; Step 5 adds conventions and surface polish, not a new instrument architecture
