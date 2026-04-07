# Phase 1: New Lane Contract

## Summary

Phase 1 is the immediate clean operator ABI break for lane transport. Replace raw `VividLanePort` and `VividStringLanePort` context fields with immutable input views and runtime-owned output builders.

This phase must leave the repo buildable. It may add simple CPU-backed `LaneBuffer` plumbing so builders have runtime-owned storage, but native optimized `LaneBufferRef` propagation and copy-on-write transport are Phase 2 work.

## Public ABI Changes

- Bump `VIVID_OPERATOR_ABI_VERSION` in `src/operator_api/types.h`.
- Remove public operator-facing `VividLanePort` and `VividStringLanePort` usage from:
  - `VividFrameContext`
  - `VividAudioContext`
  - `VividGpuContext`
- Add these replacement types to the public operator API:

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

typedef struct VividStringLaneView {
    const char* const* data;
    uint32_t length;
    uint32_t lane_set_id;
    uint32_t flags;
} VividStringLaneView;

typedef struct VividStringLaneOutput {
    void* handle;
    uint8_t (*resize)(void* handle, uint32_t length);
    void (*set)(void* handle, uint32_t index, const char* value);
    void (*commit)(void* handle, uint32_t length);
} VividStringLaneOutput;
```

- Replace the context fields with view/builder arrays:

```cpp
const VividLaneView* input_lanes;
VividLaneOutput* output_lanes;
const VividStringLaneView* input_string_lanes;
VividStringLaneOutput* output_string_lanes;
```

- Semantics:
  - `input_lanes` and `input_string_lanes` are immutable. Operators must not cast away `const` or mutate input memory.
  - Lane input arrays may be `nullptr` only when the operator context has no lane input ports of that kind.
  - Output arrays may be `nullptr` only when the operator context has no lane output ports of that kind.
  - Float lane operators call `resize(handle, length)`, write only to the returned storage, then call `commit(handle, length)`.
  - If float `resize()` returns `nullptr`, the operator must skip that lane output for the tick.
  - String lane operators call `resize(handle, length)`, call `set(handle, index, value)` for each produced value, then call `commit(handle, length)`.
  - String builders copy string contents into runtime-owned storage during `set()` or `commit()`; operators do not need to keep source string pointers alive after `process_*()` returns.
  - Builders own allocation, bounds checks, publication, and downstream lifetime. Operators no longer reason about an output `capacity` field.

## Implementation Changes

- Add minimal CPU-backed lane storage:
  - `src/runtime/graph/lane_buffer.h/.cpp`
  - `src/runtime/graph/lane_buffer_pool.h/.cpp`
  - `LaneBufferRef` may be a simple retain/release wrapper in this phase.
- Add builder adapter structs in runtime code that bridge public `VividLaneOutput` / `VividStringLaneOutput` callbacks to runtime-owned lane storage.
- Update executor context construction to populate the new view/builder fields for frame, audio, and GPU operators.
- Remove old output lane capacity semantics from public headers and from generated/operator-facing docs.
- Update `src/operator_api/child_op.h` so embedded child operators receive lane views and output builders instead of raw lane ports.
- Update all seed operators, shared operator helpers, and test operators that currently read or write:
  - `ctx->input_lanes`
  - `ctx->output_lanes`
  - `ctx->input_string_lanes`
  - `ctx->output_string_lanes`
- Update docs that teach operator authors about lane ports:
  - `src/operator_api/CLAUDE.md`
  - `mcp/opdev_docs/control_domain.md`
  - `mcp/opdev_docs/audio_domain.md`
  - `mcp/opdev_docs/gpu_domain.md`
  - relevant architecture/operator-authoring docs that mention `.data`, `.length`, or `.capacity` writes.

## Non-Goals

- Do not implement full copy-on-write lane propagation rules in this phase. Phase 2 owns passthrough ref reuse, remap copy, merge COW, and normalization.
- Do not implement GPU-backed lane storage in this phase. Phase 4 owns GPU storage-buffer backing and promotion.
- Do not redesign graph JSON unless the new ABI cannot compile without a narrowly scoped graph metadata change.
- Do not maintain old raw-lane ABI compatibility or mixed old/new operator loading.

## Test Plan

- Add or update compile/API tests proving:
  - context structs expose `VividLaneView` / `VividLaneOutput`;
  - context structs expose `VividStringLaneView` / `VividStringLaneOutput`;
  - `VIVID_OPERATOR_ABI_VERSION` changed and stale raw-lane operators fail ABI checks or fail to compile.
- Update lane test operators to compile only against the new ABI.
- Add or update runtime tests for:
  - float lane input view reads;
  - float lane output builder `resize()` plus `commit()`;
  - string lane input view reads;
  - string lane output builder `resize()` / `set()` / `commit()`;
  - builder failure returning no storage and causing an operator to skip output without raw pointer writes.
- Run targeted lane/operator API tests after the phase.
- Run `ctest --test-dir build --output-on-failure` before considering Phase 1 complete.

## Exit Criteria

- Public operator contexts no longer expose `VividLanePort*` or `VividStringLanePort*`.
- Existing raw-output lane operators are updated or fail to build.
- Seed operators and test operators compile against the new ABI.
- `ChildOp` compiles and forwards lane views/builders to child operators.
- Operator API docs and MCP opdev docs no longer teach `.data` / `.length` / `.capacity` output writes.
- The repo is buildable at the end of the phase.

## Assumptions and Defaults

- Phase 1 can use simple CPU-backed `LaneBuffer` storage to make output builders functional.
- `LaneBufferRef` is introduced now as the canonical runtime value, but optimized ref propagation is deferred to Phase 2.
- String lane builders copy string content into runtime-owned storage to avoid pointer lifetime ambiguity.
- Existing graph JSON lane ports remain valid unless a narrow metadata change is required for compilation.
- [Overview](../lane-transport-redesign.md) remains the source for global invariants and phase ordering.
