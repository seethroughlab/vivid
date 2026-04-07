# Phase 2: Runtime Transport

## Summary

Phase 2 makes `LaneBufferRef` the native lane value transported by frame and audio graph execution. Phase 1 changed the public operator contract; this phase removes the old per-edge vector-copy execution model and defines the runtime behavior for passthrough, remap, merge, normalization, structural output commits, and inspection.

This phase must leave the repo buildable. It should make lane propagation correct and reference-based, while GPU-backed storage remains Phase 4 work.

## Runtime Changes

- Promote `LaneBuffer`, `LaneBufferRef`, and `LaneBufferPool` from Phase 1 skeletons to the runtime transport primitives.
- Store lane inputs and lane outputs on compiled nodes as `LaneBufferRef` values.
- Remove old `std::vector<float>` lane fields that existed only to back the removed raw lane ABI.
- Use `LaneBufferRef` for frame-direct and audio-direct lane edges.
- Keep scalar values, string scalar values, custom ports, audio buffers, and texture routing on their existing transport paths unless a lane edge requires integration.
- Teach runtime/control-server inspection to read lane lengths and sample values from `LaneBufferRef` views instead of vector lane fields.

## Propagation Rules

- **Passthrough:** for unremapped pointwise lane output, publish the same immutable `LaneBufferRef` as the input.
- **Remap:** when an edge has a remap, materialize a new mutable CPU buffer, apply the remap element-wise, then publish it as immutable.
- **Merge:** for multiple same-provenance lane sources into one input, copy-on-write the first source and add later same-length sources element-wise.
- **Normalization:** when an operator requires uniform input lane length, materialize a new buffer only for shorter inputs that need expansion. Reuse existing refs for inputs already at the max length.
- **Structural output:** output builders write directly into runtime-owned mutable storage and commit that storage as an immutable `LaneBufferRef`.
- **Reduction output:** reduction nodes publish scalar results and no lane buffer unless the operator explicitly commits a lane output builder.
- **Errored/skipped node:** clear published output lane refs for that node for the current tick, matching scalar zeroing semantics where applicable.

## Implementation Changes

- Update frame executor lane routing to use ref propagation and the propagation rules above.
- Update audio direct-lane routing to use refs where it is not crossing the cadence snapshot boundary.
- Add lane buffer lifecycle handling:
  - retain refs when published downstream;
  - release old node output refs when replaced;
  - clear refs on graph rebuild, node removal, and executor shutdown.
- Add pool-backed mutable buffer acquisition for remap, merge, normalization, and structural output builder commits.
- Add diagnostics for runtime lane length mismatches when the compiler proved same-provenance compatibility but operators produce inconsistent runtime lengths.
- Update control-server query and graph snapshot code to expose lane lengths from the canonical refs.

## Non-Goals

- Do not implement cross-cadence snapshot replacement in this phase. Phase 3 owns frame-to-audio and audio-to-frame handoff.
- Do not implement GPU-backed storage in this phase. Phase 4 owns GPU lane buffers, promotion, upload, and readback.
- Do not reintroduce old vector staging as a parallel transport path.
- Do not change graph JSON unless runtime lane ref propagation cannot be expressed with existing compiled lane metadata.

## Test Plan

- Add `test_lane_buffer_cow` covering:
  - passthrough ref reuse;
  - remap materializes a new buffer;
  - merge copy-on-write preserves original sources;
  - normalization only copies ports that require expansion;
  - structural output builder commit publishes a new ref;
  - ref lifecycle release on output replacement.
- Update existing lane propagation tests to assert lane lengths and sample values through the new inspection path.
- Add direct audio-lane routing coverage where the route does not cross the cadence bridge.
- Add an errored/skipped node case proving stale output lane refs are not left published.
- Run targeted lane propagation and control-server query tests, then `ctest --test-dir build --output-on-failure` before considering Phase 2 complete.

## Exit Criteria

- Frame-direct lane propagation no longer copies lane vectors for pointwise passthrough.
- Audio-direct lane routing uses `LaneBufferRef` for same-cadence lane transport.
- Runtime inspection and control-server query surfaces report lane data from canonical lane refs.
- Remap, merge, normalization, structural output, and reduction behavior are covered by tests.
- The repo is buildable at the end of the phase.

## Assumptions and Defaults

- Phase 1 already removed public raw lane ports and introduced minimal builder plumbing.
- `max_lane_elements` from the overview remains the hard allocation guard for pool-backed buffers.
- `LaneBufferRef` is CPU-backed in this phase; GPU-backed storage is deferred to Phase 4.
- [Overview](../lane-transport-redesign.md) remains the source for global invariants and phase ordering.
