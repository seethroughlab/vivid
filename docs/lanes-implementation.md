# Lanes Implementation Plan

*Clean-break implementation plan for the [Lanes Architecture](lanes-architecture.md).*

Vivid is still pre-release. This plan assumes there is no requirement to preserve old graph semantics, old operator names, old runtime vocabulary, or old package behavior.

The phases below are for engineering sequencing only. They are not compatibility phases. Old spread- and auto-dup-specific semantics are being removed, not preserved.

`VIVID_PORT_SPREAD` is not part of the target model. If it survives temporarily during implementation, it does so only as an internal storage or transport detail. It must not define legality, multiplicity semantics, or public operator behavior, and it must be removed or fully demoted to a non-semantic representation detail by the end of Phase 6.

---

## Progress

| Phase | Name | Status | Scope |
|-------|------|--------|-------|
| 2A | [Lane types and compiler legality](#phase-2a-lane-types-and-compiler-legality) | Not started | ~450 LOC |
| 2B | [ABI bump and operator reclassification](#phase-2b-abi-bump-and-operator-reclassification) | Not started | ~300 LOC |
| 2C | [Lane-aware propagation](#phase-2c-lane-aware-propagation) | Not started | ~300 LOC |
| 3 | [Control-domain lane metadata](#phase-3-control-domain-lane-metadata) | Not started | ~250 LOC |
| 4 | [Audio-domain lane lifting](#phase-4-audio-domain-lane-lifting) | Not started | ~900 LOC |
| 5 | [Identity-bearing lane sets + vivid-wavetable](#phase-5-identity-bearing-lane-sets--vivid-wavetable) | Not started | ~1100 LOC |
| 6 | [Cleanup and kernel behavior](#phase-6-cleanup-and-kernel-behavior) | Not started | ~450 LOC |
| 7 | [UI and authoring](#phase-7-ui-and-authoring) | Not started | TBD |

---

## Phase 2A: Lane types and compiler legality

**Goal:** Establish the canonical lane model in runtime internals. No ABI change yet. No fallback semantics.

**Risk:** Minimal (purely additive, but introduces hard legality rules that later phases will enforce).

### Tasks

- [ ] Create `src/runtime/lane_types.h`
  - `enum class LaneBehavior { Pointwise, Structural, Reduction, Kernel }`
  - `struct LaneSet { uint32_t lane_set_id; uint32_t lane_count; bool identity_bearing; }`
- [ ] Add lane metadata to `CompiledEdge` in `src/runtime/compiled_graph.h`
  - `uint32_t lane_set_id = 0;`
  - `uint32_t lane_count = 1;`
- [ ] Add lane metadata to `CompiledNode` in `src/runtime/compiled_graph.h`
  - `LaneBehavior lane_behavior = LaneBehavior::Pointwise;`
  - `std::vector<LaneSet> output_lane_sets;`
  - `std::vector<LaneSet> input_lane_sets;`
- [ ] Add `uint32_t next_lane_set_id = 1;` to `CompiledGraph`
- [ ] Implement **Pass 2.6: Lane-set propagation** in `src/runtime/graph_compiler.cpp`
  - Insert between cadence inference (Pass 2.5) and topo sort (Pass 3)
  - Enforce **compiler legality rules**:
    - pointwise nodes may accept any number of scalar inputs
    - non-scalar inputs are legal only when all multi-lane inputs share the same `lane_set_id`
    - if exactly one upstream non-scalar lane set is present, pointwise outputs inherit that `lane_set_id`
    - scalar inputs broadcast into that lane set
    - if multiple different non-scalar lane sets are present, compilation fails
  - Structural nodes must declare whether they:
    - allocate a fresh lane set
    - preserve an upstream lane set
    - remap an upstream lane set
  - Reduction nodes consume their upstream lane set and emit scalar output or a newly declared lane set
  - Populate `CompiledEdge::lane_set_id` and `lane_count` per edge
- [ ] Add a short compiler error vocabulary for lane illegality
  - mismatched non-scalar lane sets
  - structural node missing lane-set effect declaration
  - reduction node missing output-shape declaration
- [ ] Verify all existing tests still pass where they do not depend on removed spread/autodup assumptions
- [ ] Add unit tests:
  - compile graph, verify lane metadata is populated
  - pointwise node with scalar + one multi-lane input inherits upstream `lane_set_id`
  - pointwise node with two different non-scalar `lane_set_id` values fails compilation

---

## Phase 2B: ABI bump and operator reclassification

**Goal:** Make lane behavior part of the operator contract and begin deleting spread-specific operator categories.

**Risk:** Medium (ABI bump requires full rebuild of operator dylibs).

**Depends on:** Phase 2A

### Tasks

- [ ] Add `VividLaneBehavior` to `src/operator_api/types.h`
  - `VIVID_LANE_POINTWISE`
  - `VIVID_LANE_STRUCTURAL`
  - `VIVID_LANE_REDUCTION`
  - `VIVID_LANE_KERNEL`
- [ ] Add `VividLaneBehavior lane_behavior;` to `VividOperatorDescriptor`
- [ ] Bump `VIVID_OPERATOR_ABI_VERSION` from `2u` to `3u`
- [ ] Update `VIVID_REGISTER` in `src/operator_api/operator.h`
  - default to `VIVID_LANE_POINTWISE`
  - detect `static constexpr VividLaneBehavior kLaneBehavior` via SFINAE and use it when present
- [ ] Reclassify operators around the lane-native target model
  - ordinary control and audio operators remain `POINTWISE` by default
  - structural collection producers declare `STRUCTURAL`
  - reducers declare `REDUCTION`
- [ ] Remove spread-prefixed operator names from the target model
  - `SpreadLFO` is not a target public operator; ordinary `LFO` will become lane-aware in later phases
  - `SpreadADSR` is not a target public operator; ordinary `Envelope` becomes lane-aware in later phases
  - `SpreadNoise` is replaced by a lane-native structural control generator; do not preserve the spread-prefixed type name
- [ ] Update `graph_compiler.cpp` to read `desc->lane_behavior` and store it on `CompiledNode`
- [ ] Verify all operators rebuild against ABI v3
- [ ] Verify compiler pass behavior for at least one structural operator and one reduction operator

---

## Phase 2C: Lane-aware propagation

**Goal:** Replace spread merge semantics with lane legality and lane-aware propagation. This is the first hard semantic break.

**Risk:** Medium-high (runtime behavior changes for collection propagation).

**Depends on:** Phase 2B

**Clean-break note:** Phase 2C intentionally removes the old spread-merge model. Any checked-in graph, demo, or test that depends on cycle-expand of mismatched spread lengths or implicit merge of unrelated collections is expected to fail at this phase. Those failures should be treated as intentional invalidations of removed semantics, not regressions.

### Tasks

- [ ] Replace spread merge logic in `frame_executor.cpp`
  - `1 -> N` broadcast is legal and implicit
    - preserve destination `lane_set_id`
    - do not create a new lane set
  - first non-scalar input on an empty destination establishes the destination lane set
  - `N -> N` with the same `lane_set_id` combines elementwise
  - different non-zero `lane_set_id` values are illegal
    - fail graph compilation or runtime graph activation
    - do not warn-and-continue
  - do not preserve cycle-expand behavior
  - do not preserve `lane_set_id == 0` fallback behavior
- [ ] Ensure destination lane metadata is written deterministically during propagation
- [ ] Update graph/demo expectations where they depended on spread merge wrapping
- [ ] Audit checked-in demo graphs, example graphs, and relevant tests for dependence on removed spread-merge semantics
  - identify uses of mismatched-length cycle expansion
  - identify implicit merge of unrelated non-scalar collections
  - rewrite graphs/tests to the new lane-legal form or delete them if they only demonstrate semantics being removed
  - treat failures found in this audit as expected consequences of the clean break, not compatibility regressions
- [ ] Add tests:
  - `1 -> N` broadcast preserves destination provenance
  - first non-scalar wire establishes destination lane set
  - same-provenance `N -> N` combines elementwise
  - different-provenance non-scalar inputs fail compilation
- [ ] Verify that any checked-in graph depending on removed spread-merge semantics has been either rewritten or removed
- [ ] Verify that spread merge fallback no longer exists anywhere in the executor path

---

## Phase 3: Control-domain lane metadata

**Goal:** Expose lane metadata to frame-rate operators without introducing frame-domain per-lane repeated invocation.

**Risk:** Low.

**Depends on:** Phase 2C

### Tasks

- [ ] Add to `VividFrameContext` in `src/operator_api/types.h`
  - `uint32_t lane_count;`
  - `uint32_t lane_index;`
  - `uint32_t lane_set_id;`
- [ ] Populate frame context in `frame_executor.cpp`
  - `ctx.lane_count` = resolved input lane count
  - `ctx.lane_index = 0`
  - `ctx.lane_set_id` = resolved input lane provenance
- [ ] Add `lane_set_id` to `SpreadSnapshot` in `src/runtime/snapshot_types.h`
- [ ] Propagate `lane_set_id` across the cadence bridge in `src/runtime/cadence_bridge.cpp`
- [ ] Keep this phase intentionally narrow
  - frame operators still run once per tick
  - no frame-domain pointwise lifting yet
  - the purpose is operator visibility and cadence-bridge provenance, not new execution semantics
- [ ] Add tests:
  - frame operators observe `lane_count` and `lane_set_id`
  - frame operators are still invoked once per tick
  - frame->audio bridge preserves `lane_set_id`

---

## Phase 4: Audio-domain lane lifting

**Goal:** Replace auto-dup with lane lifting. Pointwise audio operators run per lane under one model only.

**Risk:** High (audio-thread behavior and execution strategy refactor).

**Depends on:** Phase 3

### Tasks

- [ ] Add to `VividAudioContext` in `src/operator_api/types.h`
  - `uint32_t lane_count;`
  - `uint32_t lane_index;`
  - `uint32_t lane_set_id;`
  - `uint32_t lane_id;`
- [ ] Remove `channel_index` from the target public API
  - if a temporary internal shim is needed during refactor, delete it by the end of this phase
- [ ] Replace `AutoDupGroup` with `LaneLiftGroup` in `src/runtime/snapshot_types.h`
  - do not keep a typedef alias
- [ ] Remove `is_mono_autodup` from compiled/runtime state
  - replace it with lane-lifting decisions directly
- [ ] Update `audio_executor.cpp` `build()`
  - detect audio lane multiplicity from lane-bearing inputs rather than autodup heuristics
  - build `LaneLiftGroup` instances directly
- [ ] Update `audio_executor.cpp` `audio_callback()`
  - populate `lane_index`, `lane_count`, `lane_set_id`, `lane_id`
  - dispatch pointwise audio operators per lane
- [ ] Extend graph compilation to compute audio lane-lifting structure without any autodup-specific concept
- [ ] Fold spread-prefixed audio operators into lane-native ordinary operators
  - ordinary `LFO` becomes lane-aware
  - ordinary `Envelope` becomes lane-aware
  - remove `SpreadLFO` and `SpreadADSR` from the target operator set
- [ ] Keep snapshot limit policy explicit
  - do not change `SpreadSnapshot::kMaxLength` unless a concrete validation case requires it
  - if the bound changes, document the required cases and the final chosen bound
- [ ] Enforce backend equivalence constraints
  - execution strategy changes are legal only if they preserve lane order
  - preserve lane identity where applicable
  - preserve per-lane state continuity
  - preserve reduction behavior
- [ ] Add tests:
  - stereo and multivoice audio lane lifting produce expected DSP output
  - same `lane_count` with different classified `lane_set_id` values is illegal
  - execution strategies are numerically equivalent within tolerance
  - no autodup public/runtime concept remains at the end of the phase

---

## Phase 5: Identity-bearing lane sets + vivid-wavetable

**Goal:** Prove the clean lane model handles the hardest stateful case.

**Risk:** Medium.

**Depends on:** Phase 4

### Tasks

- [ ] Create `src/runtime/lane_state.h`
  - lane state key: `(node_idx, lane_id)`
  - `void* vivid_lane_state(VividAudioContext* ctx, uint32_t byte_size);`
  - semantics:
    - zero-initialized on first access
    - stable until lane retirement for that node
    - repeated same-size access returns the same storage
    - byte-size mismatch is an error
- [ ] Add `vivid_lane_state` to `VividAudioContext`
- [ ] Add `uint32_t vivid_allocate_lane_id(VividFrameContext* ctx);` for structural identity-bearing operators
  - this is the v1 allocator API unless replaced by a cleaner final API before implementation starts
- [ ] Convert `poly_voice_allocator.cpp`
  - identity-bearing structural allocator
  - allocates one stable voice `lane_set_id`
  - allocates one fresh `lane_id` per true voice creation
  - preserves `lane_id` across compaction, reordering, and release-tail retention
  - retires `lane_id` only when the voice is actually done
  - voice stealing retires the old identity and allocates a new one
- [ ] Convert `wavetable_osc.cpp`, `sub_osc.cpp`, and `analog_osc.cpp`
  - pointwise lane-preserving audio operators
  - remove per-voice slot arrays
  - use `vivid_lane_state()` for persistent oscillator state
- [ ] Convert `voice_mixer.cpp`
  - reduction consuming the voice lane set and its identity semantics
- [ ] Add tests:
  - `poly_voice_allocator` preserves `lane_id` across compaction
  - voice stealing retires old identity and allocates a new `lane_id`
  - release tails persist after gate-off
  - portamento state survives `lane_index` changes because it follows `lane_id`
  - `wavetable_osc` state follows `lane_id`, not `lane_index`
  - `voice_mixer` consumes lane identity and produces correct collapsed output

---

## Phase 6: Cleanup and kernel behavior

**Goal:** Delete remaining spread/autodup cruft, then add kernel behavior on top of the clean lane runtime.

**Risk:** Medium.

**Depends on:** Phase 5

### Tasks

- [ ] Delete spread-specific runtime branches that are no longer needed
- [ ] Remove spread-prefixed operators that have been folded into lane-native equivalents
- [ ] Remove `VIVID_PORT_SPREAD`-driven semantic special cases from core runtime logic
- [ ] Ensure any temporary `VIVID_PORT_SPREAD` usage that remains is only an internal storage or transport detail and no longer defines legality, multiplicity semantics, or public operator behavior
- [ ] Remove any temporary executor or context shims used during Phase 4
- [ ] Ensure `channel_index` is fully gone from operator-facing APIs
- [ ] Add kernel behavior on top of the cleaned lane model
  - kernel operators receive full lane-set arrays
  - run once per tick with full collection access
  - do not reintroduce spread-specific semantics
- [ ] Add one concrete kernel operator as proof case
  - lane smoothing or FFT-bin interpolation
- [ ] Add tests:
  - kernel operators read full lane sets correctly
  - no spread/autodup compatibility cruft remains in the runtime path

---

## Phase 7: UI and authoring

**Goal:** Make the lane model visible in the UI and first-class in operator authoring.

**Depends on:** Phase 6

### Tasks

- [ ] Add lane-count badges on wires and/or ports (for example `×8`)
- [ ] Make structural and reduction nodes visually legible
- [ ] Update operator scaffold templates to declare lane behavior
- [ ] Update opdev documentation to use lane vocabulary only
- [ ] Remove spread/autodup-specific authoring guidance from docs and templates

---

## Clean-Break Checkpoints

1. **End of Phase 2C**
   - lane legality is enforced strictly
   - spread merge fallback no longer exists
   - provenance is part of compilation

2. **End of Phase 4**
   - auto-dup no longer exists as a public/runtime concept
   - pointwise audio lane lifting is the only model
   - spread-prefixed audio operators are gone

3. **End of Phase 5**
   - identity-bearing lane sets are real
   - per-lane state is keyed by `lane_id`
   - `vivid-wavetable` validates the model

4. **End of Phase 6**
   - spread/autodup compatibility cruft is deleted
   - `VIVID_PORT_SPREAD` is gone or fully demoted to a non-semantic internal representation detail
   - kernel behavior sits on a clean lane-only runtime

---

## Critical files

| File | Phases | Role |
|------|--------|------|
| `src/runtime/lane_types.h` (new) | 2A | Lane vocabulary definitions |
| `src/operator_api/types.h` | 2B, 3, 4, 5 | ABI, context fields, lane behavior enum |
| `src/operator_api/operator.h` | 2B | `VIVID_REGISTER` lane behavior detection |
| `src/runtime/compiled_graph.h` | 2A, 2C, 4 | Lane metadata on edges/nodes |
| `src/runtime/graph_compiler.cpp` | 2A, 2B, 4 | Lane-set propagation, legality, audio lane lifting |
| `src/runtime/frame_executor.cpp` | 2C, 3 | Lane-aware propagation, frame metadata |
| `src/runtime/audio_executor.cpp` | 4, 6 | Lane lifting and autodup removal |
| `src/runtime/cadence_bridge.cpp` | 3 | Lane provenance in snapshots |
| `src/runtime/snapshot_types.h` | 3, 4 | Snapshot lane metadata, `LaneLiftGroup` |
| `src/runtime/lane_state.h` (new) | 5 | Per-lane persistent state service |

---

## Assumptions

- No existing graphs, packages, or users need to be preserved.
- Staging is for engineering risk only, not compatibility.
- No warning-and-continue semantics, fallback flags, alias types, or old operator-name preservation remain in the target plan.
- `SpreadLFO`, `SpreadADSR`, and `SpreadNoise` are not target-model public operators.
- The final codebase should read as lane-native, not as a spread/autodup runtime with lane features layered on top.
