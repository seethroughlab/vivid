# Phase 4: GPU Backing

## Summary

Phase 4 adds GPU storage-buffer backing as a first-class lane storage mode. Lanes that feed GPU consumers can stay on the GPU, while CPU readback happens only at explicit CPU or bridge boundaries and never on the audio callback.

This phase must leave the repo buildable. It builds on the public ABI and runtime transport introduced in Phases 1 and 2, and the realtime bridge constraints introduced in Phase 3.

## GPU Lane Model

- Extend `LaneBuffer` with backing states:
  - CPU-only;
  - GPU-only;
  - CPU + GPU with dirty flags.
- Add lazy CPU-to-GPU upload when a GPU consumer needs direct storage-buffer binding.
- Add lazy GPU-to-CPU readback only when a CPU consumer or bridge boundary explicitly requests CPU data.
- Cache staged data so multiple downstream consumers do not repeat the same upload or readback in one frame.
- Keep GPU resources owned by runtime lane buffers and released with normal `LaneBufferRef` lifecycle.

## Public GPU Context Changes

- Add lane storage-buffer inputs to `VividGpuContext`:

```cpp
WGPUBuffer* input_lane_gpu_buffers;
uint32_t* input_lane_gpu_lengths;
uint32_t input_lane_gpu_count;
```

- CPU `VividLaneView` inputs remain available only when CPU data is already materialized or a CPU boundary explicitly requested readback.
- GPU operators that need lane data at GPU scale should use storage-buffer inputs instead of uploading lane view data themselves.

## Promotion Analysis

- Add conservative compiler/runtime analysis using `gpu_lane_promotion_threshold`.
- Promote frame-domain lane buffers that:
  - feed one or more GPU consumers;
  - meet or exceed the threshold;
  - do not also feed audio/control CPU consumers unless the graph explicitly marks readback as acceptable.
- Treat unknown structural output counts as runtime-builder outputs bounded by `max_lane_elements`.
- Keep CPU-backed lanes for CPU-only chains and audio bridge paths.

## Implementation Changes

- Add WebGPU buffer creation, upload, readback, and release paths to `LaneBuffer`.
- Update GPU executor context construction to populate `input_lane_gpu_buffers`, lengths, and count.
- Add explicit CPU boundary calls for:
  - control/frame CPU operators;
  - frame-to-audio bridge publish;
  - inspection/query surfaces that need CPU lane samples.
- Add diagnostics when a GPU-preferred lane is forced into CPU readback because of a downstream CPU boundary.
- Ensure GPU readback never runs from the audio callback.

## Non-Goals

- Do not change the public builder ABI from Phase 1 unless storage-buffer binding reveals a missing required field.
- Do not promote audio-cadence lane paths to GPU-backed storage.
- Do not perform implicit GPU readback for every inspection/query; only materialize CPU data when that query explicitly needs sample values.
- Do not make GPU promotion aggressive by default. Prefer CPU backing unless the lane path clearly benefits from GPU storage.

## Test Plan

- Add `test_gpu_lane_buffer` covering:
  - GPU storage-buffer creation;
  - CPU-to-GPU upload from a CPU-backed lane buffer;
  - direct storage-buffer binding in `VividGpuContext`;
  - cached CPU readback from a GPU-backed lane buffer;
  - release of GPU resources with lane buffer lifetime.
- Add promotion tests for:
  - GPU consumer above threshold promotes;
  - CPU-only chain does not promote;
  - mixed GPU + CPU consumer does not promote unless explicitly allowed.
- Add GPU-to-audio bridge test proving CPU readback occurs on the main thread before bridge publish.
- Run targeted GPU lane tests and GPU operator tests, then `ctest --test-dir build --output-on-failure` before considering Phase 4 complete.

## Exit Criteria

- GPU operators receive storage-buffer lane inputs through `VividGpuContext`.
- Lane buffers can be CPU-backed, GPU-backed, or staged in both directions with correct dirty tracking.
- Conservative promotion avoids CPU-only and audio-cadence paths.
- CPU readback boundaries are explicit and tested.
- No audio callback path maps GPU buffers or waits on GPU readback.
- The repo is buildable at the end of the phase.

## Assumptions and Defaults

- `gpu_lane_promotion_threshold` defaults to 256 lanes.
- `max_lane_elements` remains the hard allocation guard for CPU and GPU lane buffers.
- WebGPU resource lifetime follows `LaneBufferRef` lifetime.
- [Overview](../lane-transport-redesign.md) remains the source for global invariants and phase ordering.
