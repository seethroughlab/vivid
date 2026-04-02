# Phase 5 Review Feedback

## Summary

This note reviews the implemented state of Phases 1-5 and records what still needs to converge before the fixed-cadence cleanup is actually coherent.

The direction is still strong:

- graph/compiler bridge metadata is live
- `VIVID_PORT_SCALAR` / `VIVID_PORT_AUDIO_BUFFER` naming is live
- `_fr` / `_au` operator naming is live
- `CadenceOverride` appears removed from active runtime code
- `AudioFrameBridge` has replaced `CadenceBridge` in active runtime code

What remains is narrower, but still important:

- compiler-side bridge enforcement is mostly present, but transitional scalar/audio compatibility logic remains
- Phase 4C float-CV removal is not complete
- canonical runtime docs outside this migration folder are now behind the code

## Confirmed Progress

The following migration milestones are visibly implemented in the current tree:

- Graph-level `bridge` metadata is live and compiled into `BridgeKind`
  - `src/runtime/graph.h`
  - `src/runtime/compiled_graph.h`
- `VIVID_PORT_SCALAR` and `VIVID_PORT_AUDIO_BUFFER` are now the active public numeric names
  - `src/operator_api/types.h`
- `_fr` / `_au` operator naming is active across the Phase 3 operator split
- `CadenceOverride` no longer appears in active runtime sources
- `AudioFrameBridge` exists and is now the active frame/audio bridge type
  - `src/runtime/audio_frame_bridge.h`
- The operator ABI has been bumped as part of the breaking contract changes

That is real progress. The migration is not stalled or off-track. The remaining work is about finishing the semantic cleanup and bringing the docs back into sync.

## Key Findings

### 1. Explicit bridge enforcement is mostly landed, but the compiler still carries transitional scalar/audio compatibility logic

`GraphCompiler` now does enforce the main fixed-cadence rule:

- cross-cadence connections without `bridge` are dropped
- same-cadence connections with `bridge` are dropped

That is a meaningful step forward and should be treated as landed.

However, the compiler still contains broad `SCALAR` / `AUDIO_BUFFER` compatibility classification logic in the type-validation path. In practice, the explicit-bridge rule now prevents the worst ambiguity, but the remaining compatibility branch is still a sign that the semantic cleanup is not fully finished yet.

Anchor:

- `src/runtime/graph_compiler.cpp`

Key takeaway:

- the explicit bridge model is now active
- the compiler still contains transitional mixed scalar/audio type handling that should be cleaned up before this area is considered fully converged

### 2. Phase 4C float-CV cleanup is not complete

The old scalar side-channel is still present in active runtime and operator code.

Concrete examples visible in the current tree:

- `VividAudioContext` still contains `input_float_values` and `output_float_values`
  - `src/operator_api/types.h`
- several control operators still read or write float-CV arrays directly
  - `operators/control/arpeggiator/arpeggiator.cpp`
  - `operators/control/drum_sequencer/drum_sequencer.cpp`
  - `operators/control/tracker/tracker.cpp`
- tests still describe and exercise float-CV behavior
  - `tests/test_operator_sweep.cpp`
  - `tests/test_signal_port.cpp`

This is the biggest remaining semantic gap between the migration docs and the implementation.

Key takeaway:

- fixed-cadence naming and bridge structure have advanced further than the audio-context cleanup
- Phase 4C should not be considered complete yet

### 3. Canonical runtime docs are behind the code

The migration docs are now ahead of the main engineering docs.

Several canonical docs still refer to `CadenceBridge` and/or describe float-CV paths that the migration intends to remove. Examples:

- `docs/ARCHITECTURE.md`
- `docs/runtime/architecture.md`
- `docs/runtime/runtime_core.md`

This creates real onboarding and implementation risk. A reader following the main runtime docs today will still get an outdated mental model of the bridge and cadence boundary.

Key takeaway:

- the migration-specific docs are no longer enough on their own
- the canonical runtime docs need to catch up before the architecture is teachable again from the main doc set

## What This Means Before Phase 6

The migration direction still looks sound. Nothing in this review suggests rethinking fixed cadence, `_fr` / `_au`, or explicit bridge edges.

What it does suggest is that Phase 5 should not be treated as "everything through bridge cleanup is complete."

Before moving on cleanly, we still need to:

- finish the semantic cutover in the compiler/runtime
- finish removing float-CV side-channel state
- bring the canonical runtime docs back into sync with the implementation

This is cleanup and convergence work, not a reason to reopen the architecture.

## Validation Limits

This review is based on static inspection of the current source tree.

Confidence is still high because the remaining mismatches are visible in active code paths:

- compiler type-validation logic
- audio-context fields
- operator/test usage
- canonical runtime docs

If partial tests or runtime checks are still passing in an existing build tree, that does not negate the static mismatches documented here.

## Recommended Immediate Follow-Ups

1. Finish compiler cleanup around scalar/audio compatibility so the explicit bridge model is not sharing space with transitional mixed-type logic.
2. Complete Phase 4C by removing float-CV side-channel state from `VividAudioContext`, operators, and the tests that still depend on it.
3. Update canonical runtime docs to reflect `AudioFrameBridge`, fixed cadence, and the current bridge semantics.
4. Run a focused grep-based cleanup pass for stale `CadenceBridge`, `input_float_values`, and `output_float_values` references once the code cleanup lands.
