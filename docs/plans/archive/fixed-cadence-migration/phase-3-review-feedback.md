# Phase 3 Review Feedback

## Summary

This note reviews the implemented state of Phases 1-3 and records the remaining risks before Phase 4 starts.

The direction is still sound:

- bridge metadata carry-through is in place
- `SCALAR` / `AUDIO_BUFFER` naming is live
- `_fr` / `_au` operator variants are being built

But the public-surface cutover is not complete yet. The main remaining risks are stale bare-name assets, lingering internal dependence on old dual-cadence classes, and legacy bare registrations still present in-tree.

## Key Findings

### 1. Active tests and demo graphs still target old bare operator names

Multiple tests and active demo graphs still use the old public surface (`Clock`, `LFO`, `Envelope`, `clock.dylib`, `lfo.dylib`, `envelope.dylib`) even though Phase 3 moved the built plugin targets to `_fr` / `_au`.

Concrete examples:

- `tests/test_audio_cadence_sequencer.cpp`
- `tests/test_frame_lane_lifting.cpp`
- `tests/test_cadence_inference.cpp`
- active demo graphs under `graphs/audio/`

This is not just docs drift. These assets must migrate before the new public surface is actually coherent.

### 2. Internal compile-time consumers still depend on old dual-cadence classes

The old headers still define dual-cadence types, and internal consumers still use them directly.

Concrete examples:

- `operators/control/lfo/lfo.h`
- `operators/control/envelope/envelope.h`
- `tests/test_child_op.cpp`

This may be intentional temporary reuse, but it is still a real Phase 4 risk and should not be mistaken for a completed split.

### 3. Old bare operator implementations remain registered in-tree

Legacy bare operator source files still contain `VIVID_REGISTER(...)` for the old public names even though CMake now builds only `_fr` / `_au` plugin targets.

Concrete examples:

- `operators/control/clock/clock.cpp`
- `operators/control/envelope/envelope.cpp`
- `operators/control/lfo/lfo.cpp`

This leaves two sources of truth in the tree. Even if those old plugins are not currently built, the legacy registration presence is attracting stale tests and includes and makes the migration harder to reason about.

## What This Means For Phase 4

Phases 1 and 2 still look solid, and Phase 3 is directionally correct in CMake and naming.

What remains is to finish the cutover cleanly:

- active tests need to stop staging bare-name dylibs
- active graphs need to stop using bare paired operator names
- in-repo internal consumers need to stop depending on the old dual-cadence classes, or that dependency needs to be explicitly documented as temporary
- legacy bare registrations need to be removed or clearly isolated as non-public leftovers

This does not suggest changing the migration architecture. It means the repo still needs a final public-surface cleanup before the fixed-cadence model is internally consistent.

## Validation Limits

Targeted `ctest` runs passed from the existing build tree:

- `test_frame_lane_lifting`
- `test_cadence_inference`
- `test_graph_compiler_init`
- `test_graph_compiler`

A targeted rebuild attempt failed while `FetchContent` tried to update `webgpu`, so this review could not verify a completely clean rebuild from source.

Because of that, the stale bare-name references called out here are treated as high-confidence static breakage risks, even though the current build tree still runs some tests successfully.

## Recommended Immediate Follow-Ups

- Migrate active tests from bare paired operator names to `_fr` / `_au`
- Migrate active demo graphs under `graphs/audio/` away from bare paired operator names
- Decide whether old dual-cadence header types remain as temporary internal reuse or should be removed now
- Remove or isolate legacy bare `VIVID_REGISTER(...)` implementations so the source tree has one clear public operator surface

