# Reusable Synth Filter Platform V1/V2

## Summary
Implement `#3` as a **new synth-oriented dual-filter core operator** built on the existing shared filter DSP, not as a replacement for the current `Filter` operator.

V1 should be **operator-first** and **routing-first**:
- add one strong reusable audio primitive for instrument-style filter authoring,
- preserve the existing `Filter` as the stable single-stage primitive,
- support lane-aware polyphonic behavior through Vivid's current audio lane-lifting model,
- give packages a readable, reusable serial / parallel / split filter surface they can wrap inside Step 1 modules and target from Step 2 modulation assignments.

Key boundaries:
- no hard-coded synth engine or mandatory signal path,
- no package-specific voicing logic moved into core,
- no full modulation matrix or arbitrary filter-rack graph inside one node,
- no breakage or semantic drift for existing `Filter` graphs.

## Rationale
### Why this is useful
- Vivid already has a capable single-stage `Filter`, but coherent synth packages still have to assemble dual-filter behavior from graph glue rather than a stable instrument-grade primitive.
- The remaining gap is not mainly "more filter modes"; it is reusable routing, predictable per-voice behavior, and a cleaner package authoring surface.
- A strong dual-filter primitive makes package graphs smaller, easier to expose through module surfaces, and easier to modulate coherently.

### Why this should be a new operator
- The current `Filter` already appears in graphs and tests as the generic single-stage filter primitive.
- Extending `Filter` into a dual-stage routing platform would increase migration risk, expand an already-stable contract, and blur the simple-vs-advanced split.
- V1 should keep `Filter` intact and add a second reusable tier on top of the same DSP foundation.

## Normal Workflow
### Synth-package workflow
- A package author drops one `DualFilter` node into a voice graph between oscillator/voice shaping and output mixing.
- The author exposes a small set of filter controls through the Step 1 module surface: routing, cutoff A/B, resonance A/B, drive A/B, split frequency, and any stage enable controls worth surfacing.
- The author declares Step 2 modulation destinations against those exposed params or directly against the internal `DualFilter` params.
- A user tweaks routing and filter tone from one coherent module surface instead of reopening several internal wires and mixers.

### Generic-audio workflow
- A non-synth package can still use the same operator as a compact dual-stage audio shaper.
- The value is reusable routing plus musical filter flavors, not synth-specific hard-coding.

## V1 Key Changes
- Add one new core audio operator type: `DualFilter`.
- Keep the current `Filter` operator unchanged and continue backing both operators with the shared `filter_dsp` layer.
- `DualFilter` exposes three inspector groups:
  - `Filter A`
  - `Filter B`
  - `Routing`
- Each filter stage exposes:
  - `enabled`
  - `mode`
  - `cutoff`
  - `resonance`
  - `drive`
  - `keytrack`
- The routing group exposes:
  - `routing` with choices `serial_ab`, `serial_ba`, `parallel`, `split`
  - `parallel_balance`
  - `split_freq`
  - `output_gain`
- `DualFilter` ports:
  - `input`
  - `output`
  - `a_cutoff_cv`
  - `b_cutoff_cv`
  - `a_resonance_cv`
  - `b_resonance_cv`
  - `a_cutoff_mod`
  - `b_cutoff_mod`
  - `frequencies`
  - standard analysis ports
- Routing behavior is fixed in v1:
  - `serial_ab`: full-range signal through A then B
  - `serial_ba`: full-range signal through B then A
  - `parallel`: full-range signal into both stages, outputs blended by `parallel_balance` with normalized gain at center
  - `split`: source split by a complementary crossover at `split_freq`, low band into A and high band into B, then recombined
- Stage-disable behavior is fixed in v1:
  - disabled stage is passthrough in serial modes
  - disabled stage contributes silence in parallel and split modes
- Shared DSP work:
  - refactor `operators/shared/filter_dsp/*` so one-stage processing remains reusable by `Filter`
  - add the minimum extra state/helpers needed for `DualFilter` routing and split crossover without forking the existing algorithms
- UI expectation:
  - no custom inspector required in v1
  - standard grouped params and existing `OperatorInfo` metadata are enough

## Public Interfaces / Types
- New public operator type: `DualFilter`
- Stable param naming uses `a_` / `b_` prefixes plus routing params:
  - `a_enabled`, `a_mode`, `a_cutoff`, `a_resonance`, `a_drive`, `a_keytrack`
  - `b_enabled`, `b_mode`, `b_cutoff`, `b_resonance`, `b_drive`, `b_keytrack`
  - `routing`, `parallel_balance`, `split_freq`, `output_gain`
- Stable port naming uses:
  - `input`, `output`
  - `a_cutoff_cv`, `b_cutoff_cv`
  - `a_resonance_cv`, `b_resonance_cv`
  - `a_cutoff_mod`, `b_cutoff_mod`
  - `frequencies`
- Semantic metadata should reuse existing conventions:
  - cutoff params and `frequencies` port use `frequency_hz`
  - resonance params and CV ports use `resonance`
  - drive and output gain use existing amplitude-style tags where appropriate
- No graph JSON schema changes are needed beyond normal node param storage.

## Lane-Aware / Polyphonic Behavior
- `DualFilter` is pointwise lane-aware in the same sense as the current `Filter`.
- Per-stage internal filter memory is keyed by `lane_id`, so filter state follows voice identity through lane reordering and compaction.
- Scalar CV inputs are shared across voices.
- `a_cutoff_mod` and `b_cutoff_mod` are lane-array inputs indexed by `lane_index` in lifted audio chains.
- `frequencies` is the shared lane-array note-frequency input for stage keytracking.
- V1 does not introduce separate lane sets per stage, cross-stage voice remapping, or any new lane transport.

## V1 Non-Goals
- No mutation of the current `Filter` contract into a dual-stage operator
- No arbitrary 3-stage or N-stage filter rack
- No graph-inside-operator routing editor
- No stereo, mid/side, or left/right-independent routing model in v1
- No package/module schema changes specifically for Step 3
- No filter-morph matrix, routing CV, or per-stage tap outputs in v1

## V2 Follow-Up
- stereo-aware or mid/side variants
- optional stage tap outputs for advanced graph composition
- richer crossover types and routing morphing
- more explicit visual routing affordances or custom inspector UX
- broader filter-flavor expansion only if package use shows a real gap beyond the current shared DSP set

## Test Plan
- DSP parity:
  - `Filter` remains behaviorally unchanged after shared-DSP refactor
  - `DualFilter` with one stage disabled matches the relevant single-stage `Filter` behavior closely enough for the same mode/settings
- Routing behavior:
  - `serial_ab` and `serial_ba` produce measurably different output when stage settings differ
  - `parallel` responds correctly to `parallel_balance`
  - `split` sends low/high energy to the expected branches around `split_freq`
- Lane-aware audio:
  - per-lane cutoff modulation affects the correct voice
  - keytracking uses the aligned `frequencies` lane input
  - lane state remains stable across compaction/reordering
- Surface/query:
  - `DualFilter` exposes grouped params, choices, semantic metadata, and ports through normal operator inspection surfaces
- Package-backed integration:
  - a Step 1 module can expose `DualFilter` params cleanly
  - Step 2 modulation destinations can target `DualFilter` params without special-case runtime work
- Regression:
  - existing `Filter` graphs, presets, and test expectations stay intact

## Assumptions And Defaults
- Chosen default: add a new `DualFilter` operator rather than extending `Filter`
- Chosen default: keep `Filter` as the simple single-stage primitive
- Chosen default: split mode means low band into A, high band into B
- Chosen default: `parallel_balance` is normalized rather than raw summed gain
- Chosen default: standard grouped-param inspector is sufficient in v1
- Chosen default: no new graph/module schema changes are required for Step 3
- Step 3 is meant to compose cleanly with Step 1 module surfaces and Step 2 local modulation, not replace them
