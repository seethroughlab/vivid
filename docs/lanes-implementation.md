# Lanes Implementation

*Implementation record for the [Lanes Architecture](lanes-architecture.md).*

## Progress

| Phase | Name | Status |
|-------|------|--------|
| 2A | Lane types and compiler legality | Complete |
| 2B | ABI bump and operator reclassification | Complete |
| 2C | Lane-aware propagation | Complete |
| 3 | Control-domain lane metadata | Complete |
| 4 | Audio-domain lane lifting | Complete |
| 5 | Identity-bearing lane sets + vivid-wavetable | Complete |
| 6 | Cleanup and kernel behavior | Complete |
| A | Prove execution model | Complete |
| B | Structural reshaping | Complete |
| C | Frame-domain per-lane lifting | Complete |
| D | Breadth proof cases | Complete |
| E | GPU compute-backed lane evaluation | Complete |
| F | Polish and naming | Complete |
| 7 | UI and authoring | Not started |

---

## What was delivered

### Foundation (Phases 2A–6)

Built the lane model from scratch:
- Lane types (`LaneBehavior`, `LaneSet`, `LaneExecutionStrategy`) in `lane_types.h`
- Compiler Pass 2.6: lane-set propagation with strict legality enforcement
- ABI v3 with `VividLaneBehavior` on operator descriptors
- Lane-aware frame propagation (no cycle-expand, no modulo indexing)
- Audio-domain lane lifting via `LaneLiftGroup` (InstancePerLane)
- Identity-bearing lane sets with `vivid_lane_state()` keyed by `lane_id`
- `LaneStateService` for per-lane persistent state
- Kernel behavior proof (LaneSmoothOp cross-lane averaging)
- Spread-prefixed operators (SpreadLFO, SpreadADSR, SpreadNoise) removed

### Execution model proof (Phase A)

- Full 4-lane InstancePerLane vs LoopBased equivalence (identical output within tolerance)
- Identity compaction: state follows `lane_id`, not positional index
- Configurable `max_loop_lanes` with diagnostic logging (replaces hardcoded limit)
- LoopBased→LoopBased audio routing copies full multi-lane buffer
- Operator migrations: Gain, Filter, Bitcrush, LFO, Envelope all use `vivid_lane_state()`

### Reshape operators (Phase B)

- Repeat (Structural): broadcast scalar to N lanes
- Tile (Structural): tile short pattern to target length
- Select (Reduction): pick one lane, reduce to scalar (`lane_set_id=0`)

### Frame-domain lifting (Phase C)

- LoopBased dispatch in frame executor with per-lane `vivid_lane_state()`
- Compiler Pass 4d assigns LoopBased to strategy-independent frame operators
- Identity-bearing lane_ids propagation on frame path
- `VividFrameContext` extended with `lane_id`, `lane_state_fn`, etc.

### Breadth proof (Phase D)

- FFTAnalysis reclassified as Structural (spectrum output creates new lane set)
- 256-lane and 512-lane frame LoopBased lifting verified
- FFT pipeline: Repeat → FFTAnalysis → per-bin LoopBased processing

### GPU compute proof (Phase E)

- Compute shader helpers in `gpu_common.h`: `create_compute_shader`, `create_compute_pipeline`, `create_storage_buffer`, `create_readback_buffer`, `dispatch_compute`
- CPU vs GPU direct comparison: same operation, same input, output matches
- Lane ordering preserved through GPU dispatch
- Reduction on GPU: sum 128 lanes to scalar, matches CPU

### Polish and naming (Phase F)

- Transport naming alignment: `VIVID_PORT_SPREAD` → `VIVID_PORT_LANE_ARRAY`, `VividSpreadPort` → `VividLanePort`, `input_spreads` → `input_lanes`, etc. Clean break, no aliases.
- Formal planner boundary: `plan_audio_lane_strategy()` and `plan_frame_lane_strategy()` extracted from inline Pass 4c/4d logic. Returns `AudioLanePlan` / `FrameLanePlan` structs capturing all execution-shape data.

---

## Remaining work

### Phase 7: UI and authoring

- Lane-count badges on wires/ports
- Visual distinction for structural and reduction nodes
- Operator scaffold templates with lane behavior declarations
- Opdev documentation updated to lane vocabulary

### Future proof cases

- Lane-driven GPU visual instancing (FFT spectrum → instanced shapes)
- Structural particle emitter → per-particle GPU integration
- Stronger Phase E: same semantic operator with CPU and GPU implementations compared through the runtime
- Identity-bearing and reduction coverage on GPU compute path

---

## Critical files

| File | Role |
|------|------|
| `src/runtime/lane_types.h` | Lane vocabulary: LaneBehavior, LaneSet, LaneExecutionStrategy |
| `src/operator_api/types.h` | VividLanePort, VividFrameContext/VividAudioContext lane fields |
| `src/operator_api/operator.h` | VIVID_REGISTER lane behavior detection |
| `src/operator_api/gpu_common.h` | Compute shader helpers |
| `src/runtime/compiled_graph.h` | Lane metadata on edges/nodes, execution strategy fields |
| `src/runtime/graph_compiler.cpp` | Lane-set propagation, planner functions, buffer allocation |
| `src/runtime/frame_executor.cpp` | Lane-aware propagation, LoopBased frame dispatch |
| `src/runtime/audio_executor.cpp` | InstancePerLane and LoopBased audio dispatch |
| `src/runtime/cadence_bridge.cpp` | Lane provenance in cross-cadence snapshots |
| `src/runtime/lane_state.h` | Per-lane persistent state service |
