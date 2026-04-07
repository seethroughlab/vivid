# Lane Transport Redesign

## Context

The current lane transport copies flat `std::vector<float>` arrays per-tick between nodes. It has four problems:

1. **Silent truncation**: `LaneSnapshot` caps at 64 elements crossing the audio bridge — data loss with no warning
2. **Buffer overrun**: `kMaxLaneCapacity = 1024` pre-allocated buffers — writing more corrupts memory
3. **Wasteful copying**: Pointwise operators copy the full array even though they don't modify it
4. **No GPU backing**: Lane data is always CPU float arrays; GPU operators must upload per frame

The system must support 100k+ lanes (particle systems), GPU-backed lane storage, and no artificial limits.

## Design: Shared Immutable LaneBuffers with Copy-on-Write

**Core abstraction** — `LaneBuffer` (new, `src/runtime/graph/lane_buffer.h`):
- Ref-counted (intrusive, not `shared_ptr` — avoids control-block allocation)
- Immutable: `cpu_data()` returns `const float*`; mutation requires `mutable_copy()` (COW)
- Dual-backed: CPU memory or GPU storage buffer (`WGPUBuffer`)
- Lazy bidirectional staging: CPU-backed buffers lazily upload to GPU; GPU-backed buffers lazily readback to CPU shadow
- Accessed via `LaneBufferRef` (retain/release pointer wrapper)

**Pool** — `LaneBufferPool` (new, `src/runtime/graph/lane_buffer_pool.h`):
- Free-list per size class (power-of-two buckets up to 16M floats)
- Pre-warmed during `build()` for audio-thread safety
- Lock-free acquire/release for real-time callback

**GPU promotion threshold** — The compiler (Pass 2.6) already knows lane counts per node. A new pass adds demand-driven GPU-backing analysis:
- Walk backward from GPU operators, marking upstream lane sources as "GPU-preferred" if `lane_count > threshold` (default ~256)
- CPU-only chains (control→audio bridge) never promote, avoiding pointless GPU readback
- Threshold is a graph-level option, overridable per-node via hint

**Dynamic cross-cadence snapshots** — Replace `LaneSnapshot { float data[64]; }` with `DynamicLaneSnapshot { float* data; uint32_t length; }` pointing into a pre-allocated arena in `ParamSnapshot`. Arena sized during `AudioFrameBridge::build()` from compiled edge metadata. Audio thread reads only; main thread writes before atomic swap.

## Phases

### Phase 1: Remove Capacity Limits (no ABI break)

Ship first — solves the user-visible truncation and crash bugs.

- Remove `kMaxLaneCapacity = 1024` constant; size `out_lane_buf` per-node from Pass 2.6 lane count metadata
- Add runtime overflow detection: if operator writes more than allocated capacity, grow buffer on main thread
- Replace `LaneSnapshot { float data[64] }` with `DynamicLaneSnapshot` backed by pre-allocated arena in `ParamSnapshot`
- Remove `min(length, kMaxLength)` cap in `audio_frame_bridge.cpp` and `audio_executor.cpp`

**Files:**
- `src/runtime/graph/graph_compiler_internal.h` — remove `kMaxLaneCapacity`
- `src/runtime/graph/graph_compiler_init.cpp` — dynamic buffer sizing
- `src/runtime/graph/graph_compiler.cpp` — same
- `src/runtime/graph/frame_executor.cpp` — dynamic capacity, overflow detection
- `src/runtime/graph/snapshot_types.h` — `DynamicLaneSnapshot`
- `src/runtime/audio/audio_frame_bridge.h/.cpp` — arena pre-allocation, remove 64-cap
- `src/runtime/graph/audio_executor.cpp` — remove 64-cap on analysis output

### Phase 2: LaneBuffer + COW for Frame Executor (no ABI break)

Eliminates redundant copies in pointwise chains.

- New files: `lane_buffer.h/.cpp`, `lane_buffer_pool.h/.cpp`
- Add `LaneBufferRef` fields to `CompiledNode` alongside existing `vector<float>` (dual-write during transition)
- Frame executor wire propagation: pointwise passthrough = ref bump (zero copy); merge = `mutable_copy()` + element-wise add
- Structural operators get pool-acquired output buffers
- Detection: if `c_out_lanes[p].length` stays 0 after `process_frame()` and node is Pointwise, passthrough input ref

**Files:**
- New: `src/runtime/graph/lane_buffer.h`, `lane_buffer.cpp`, `lane_buffer_pool.h`, `lane_buffer_pool.cpp`
- `src/runtime/graph/compiled_graph.h` — add `LaneBufferRef` fields
- `src/runtime/graph/frame_executor.cpp` — ref-based propagation
- `src/runtime/graph/graph_compiler_init.cpp` — init buffer refs

### Phase 3+4: GPU-Backed Lanes + ABI Bump (ship together)

GPU operators can bind lane data directly as storage buffers.

- Implement `LaneBuffer::make_gpu()`, lazy CPU readback via `wgpuBufferMapAsync`
- Compiler pass: demand-driven GPU-backing analysis (backward walk from GPU operators, threshold-based promotion)
- Add `WGPUBuffer* input_lane_gpu_buffers` to `VividGpuContext` (ABI v10)
- Fallback: v9 operators get CPU data via `input_lanes[p].data` as before (executor calls `cpu_data()`)
- Cross-domain safety: GPU→audio bridge path calls `cpu_data()` on main thread, copies into snapshot arena

**Files:**
- `src/runtime/graph/lane_buffer.h/.cpp` — GPU backing implementation
- `src/runtime/graph/frame_executor.cpp` — GPU lane staging
- `src/runtime/graph/graph_compiler.cpp` — GPU-preference analysis pass
- `src/operator_api/gpu_operator.h` — new `VividGpuContext` fields
- `src/operator_api/types.h` — bump `VIVID_OPERATOR_ABI_VERSION` to 10

### Phase 5: Audio Pool Pre-warming

Audio-cadence structural operators can produce new lane buffers without allocation.

- In `AudioExecutor::build()`: compute max output lane count per audio structural node, warm pool
- Audio callback acquires from pool (lock-free); release returns to pool
- Fallback: pool exhausted → use scratch buffer + diagnostic log

**Files:**
- `src/runtime/graph/lane_buffer_pool.h/.cpp`
- `src/runtime/graph/audio_executor.cpp`

## Key Design Answers

**How does GPU-backed data expose `float*` to CPU operators?**
`cpu_data()` lazily allocates a CPU shadow and does synchronous GPU readback (`wgpuBufferMapAsync` + poll). Runs on main thread at 60Hz. Shadow is cached and shared across downstream consumers of the same ref.

**How does immutability interact with lane merging?**
First source → `mutable_copy()` (returns self if refcount==1, else copies from pool). Add subsequent sources element-wise into the now-mutable buffer.

**How does GPU→audio bridge work?**
`push_to_audio()` (main thread) calls `cpu_data()` on GPU-backed ref, copies floats into pre-allocated snapshot arena. Audio thread never touches GPU resources.

**When are lanes GPU-backed vs CPU?**
Compiler demand analysis: if a lane source has `lane_count > threshold` AND feeds a downstream GPU operator (directly or transitively), allocate GPU-backed. Otherwise CPU. Threshold defaults to ~256, overridable per-graph.

## Verification

- **Phase 1**: Extend `test_lane_capacity` for >1024 lanes. Extend `test_lane_bridge_snapshot` for >64 lanes through audio bridge.
- **Phase 2**: New `test_lane_buffer_cow` — verify ref passthrough for pointwise, COW for merge, pool acquire/release.
- **Phase 3+4**: New `test_gpu_lane_buffer` — GPU storage buffer creation, lazy readback, direct binding.
- **Phase 5**: Extend audio lane tests for pool-based allocation under multi-lane scenarios.
- Full `ctest` pass after each phase. `rebuild_package` for linked packages after ABI bump.
