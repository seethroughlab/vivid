# Phase 3: Cross-Cadence and Realtime Safety

## Summary

Phase 3 rebuilds lane handoff between frame cadence and audio cadence around stable immutable snapshots or retained `LaneBufferRef` views. The fixed copied lane snapshot model is removed from the bridge path, and audio callback lane handling becomes explicitly realtime-safe.

This phase must leave the repo buildable. It may use CPU-accessible lane buffers only; GPU-backed lane buffers and GPU readback policy are Phase 4 work.

## Cross-Cadence Model

- Replace frame-to-audio lane snapshots with immutable lane snapshot views backed by preallocated CPU-accessible lane data or retained `LaneBufferRef` handles.
- Replace audio-to-frame lane snapshots with the same model.
- `AudioFrameBridge::build()` computes and allocates all bridge lane storage needed for the compiled graph.
- `AudioExecutor::build()` computes and prewarms all lane storage reachable from audio-cadence structural operators and audio bridge destinations.
- Frame-to-audio handoff publishes immutable lane snapshots before the atomic parameter snapshot swap.
- Audio-to-frame handoff publishes immutable lane snapshots only after the audio callback has committed output storage.
- Snapshot data remains valid until the next bridge publish for the same buffer slot.

## Realtime Invariants

The audio callback must never:

- allocate heap memory;
- resize heap vectors;
- acquire locks;
- block or wait on another thread;
- map GPU buffers;
- perform GPU readback;
- mutate shared frame-side lane buffers.

If an audio-cadence output builder cannot provide storage from prewarmed capacity, it returns failure, records a rate-limited diagnostic, and drops that lane output for the tick.

## Implementation Changes

- Update `AudioFrameBridge` snapshot types to carry lane snapshot views or retained refs instead of fixed copied arrays.
- Update bridge build logic to preallocate bridge lane storage for all frame-to-audio and audio-to-frame lane edges.
- Update `AudioExecutor` build logic to prewarm pool buckets for audio structural lane outputs and downstream audio lane routes.
- Update audio callback code to write lane outputs only through prewarmed output builders.
- Replace audio callback lane `assign()` / resize paths with fixed-capacity writes or builder commits.
- Add diagnostics for dropped lane outputs caused by insufficient prewarmed capacity.
- Ensure diagnostics are rate-limited and do not allocate in the callback.

## Non-Goals

- Do not implement GPU-backed lane buffers or GPU readback in this phase.
- Do not promote lanes based on GPU consumers in this phase.
- Do not change the public operator ABI introduced in Phase 1 unless this phase finds a realtime safety bug in the builder contract.
- Do not support dynamic audio-thread allocation as a fallback.

## Test Plan

- Add bridge tests above 64 lanes in both directions:
  - frame-to-audio lane input;
  - audio-to-frame lane output.
- Add audio direct-lane routing tests proving prewarmed storage is used without heap growth.
- Add audio structural output tests for builder commit under multi-lane scenarios.
- Add builder failure tests proving the callback drops output and emits a rate-limited diagnostic without allocating.
- Add or extend instrumentation tests that detect allocations, vector growth, locks, blocking calls, and GPU access in callback lane paths where practical.
- Run targeted audio bridge and audio lane tests, then `ctest --test-dir build --output-on-failure` before considering Phase 3 complete.

## Exit Criteria

- Fixed 64-float lane snapshots are removed from both bridge directions.
- Frame-to-audio and audio-to-frame lane bridge tests pass above 64 lanes.
- Audio callback lane paths do not allocate, block, lock, resize heap vectors, map GPU buffers, or read back GPU data.
- Bridge snapshot lifetime is documented and covered by tests.
- The repo is buildable at the end of the phase.

## Assumptions and Defaults

- Phase 2 has made `LaneBufferRef` the canonical same-cadence lane transport.
- Bridge storage may be CPU-accessible only in this phase.
- Audio builder failure means “drop lane output for this tick,” not “allocate a fallback buffer.”
- [Overview](../lane-transport-redesign.md) remains the source for global invariants and phase ordering.
