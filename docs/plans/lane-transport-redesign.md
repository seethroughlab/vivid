# Lane Transport Redesign

## Context

Lane transport should look like a native Vivid primitive, not a compatibility layer over raw `std::vector<float>` copies. This redesign is a clean break: operators, packages, tests, docs, and internal runtime APIs may change so the final system reads as if this was always the intended model.

The redesign removes these historical constraints:

1. `LaneSnapshot` fixed-size 64-float bridge payloads.
2. `kMaxLaneCapacity = 1024` staging buffers and string-lane pointer arrays.
3. Raw operator output lane pointers with caller-owned capacity.
4. Per-tick vector copying through pointwise chains.
5. CPU-only lane storage that forces GPU operators to upload lane data themselves.

The target is one lane model across frame, audio, bridge, and GPU paths: immutable lane views, runtime-owned output builders, shared `LaneBuffer` storage, copy-on-write mutation, and optional GPU storage-buffer backing.

## Target Contract

`LaneBuffer` is the runtime-owned backing store for lane data. It is reference counted, immutable once published, and may be CPU-backed, GPU-backed, or both after staging. Runtime code transports `LaneBufferRef` values between nodes instead of copying lane vectors by default.

Operator-facing lane inputs are immutable views. Operator-facing lane outputs are runtime-owned builders, not raw `float*` plus `capacity`.

```cpp
typedef struct VividLaneView {
    const float* data;
    uint32_t length;
    uint32_t lane_set_id;
    uint32_t flags;
} VividLaneView;

typedef struct VividLaneOutput {
    void* handle;
    float* (*resize)(void* handle, uint32_t length);
    void (*commit)(void* handle, uint32_t length);
} VividLaneOutput;
```

String lanes use the same contract shape with immutable `const char* const*` input views and runtime-owned output builders for pointer arrays.

## Global Invariants

- `VIVID_OPERATOR_ABI_VERSION` bumps as part of Phase 1; no old raw-lane ABI support is retained.
- `LaneBufferRef` is the canonical runtime lane value; old vector staging is deleted rather than maintained in parallel.
- Operators never mutate input lane memory.
- Operators produce lane outputs only through runtime-owned builders.
- `max_lane_elements` is the graph/runtime allocation guard. Initial value: 16,777,216 floats per lane buffer.
- `gpu_lane_promotion_threshold` defaults to 256 lanes.
- Unknown structural output counts are accepted at runtime through output builders, bounded by `max_lane_elements`.
- The audio callback never allocates, locks, blocks, maps GPU buffers, or resizes heap vectors.
- Graph JSON changes only where needed to express lane policy or GPU promotion policy.

## Execution Rule

Implement phases in order. Each phase page must be specific enough to execute independently and must leave the repo buildable unless the page explicitly marks itself as a branch-only checkpoint.

## Phase Pages

1. [Phase 1: New Lane Contract](lane-transport-redesign/phase-1-new-lane-contract.md)
   Break the operator ABI, introduce immutable lane views and output builders, remove old raw lane ports, and update seed/test operators and authoring docs.

2. [Phase 2: Runtime Transport](lane-transport-redesign/phase-2-runtime-transport.md)
   Make `LaneBufferRef` the native frame/audio lane transport and define passthrough, remap, merge, normalization, structural output commit, and inspection behavior.

3. [Phase 3: Cross-Cadence and Realtime Safety](lane-transport-redesign/phase-3-cross-cadence-realtime.md)
   Rebuild frame-to-audio and audio-to-frame lane handoff around stable snapshots/views, prewarmed storage, and strict audio-callback invariants.

4. [Phase 4: GPU Backing](lane-transport-redesign/phase-4-gpu-backing.md)
   Add GPU storage-buffer backing, GPU context lane-buffer fields, conservative promotion, and explicit CPU readback boundaries.

5. [Phase 5: Cleanup and Verification](lane-transport-redesign/phase-5-cleanup-verification.md)
   Delete obsolete vector staging and fixed snapshots, update docs and graph/query surfaces, rebuild packages, and complete the final full test pass.

## Final Verification Target

- ABI/API tests prove old raw-output lane operators fail to build or have been updated.
- Lane transport tests cover more than 1024 lanes for frame and audio direct routing.
- Bridge tests cover more than 64 lanes in both frame-to-audio and audio-to-frame directions.
- Copy-on-write tests cover passthrough ref reuse, remap copy, merge, normalization, structural output commit, and lifecycle release.
- String-lane tests cover more than 1024 entries.
- Audio callback tests prove lane copy/routing/output builders do not allocate, resize heap vectors, lock, block, or touch GPU resources.
- GPU lane buffer tests cover direct storage-buffer binding, conservative promotion, explicit CPU readback, and GPU-to-audio bridge readback on the main thread.
- `ctest --test-dir build --output-on-failure` passes after the implementation milestones and final package rebuild.
