# Lanes Remaining Work — Status

All six phases (A–F) from the original phased plan have been delivered and tested. This document records what was delivered and what remains as future work.

---

## Delivered

### Phase A: Prove the execution model ✓

- Full 4-lane InstancePerLane vs LoopBased equivalence test (identical output within tolerance)
- Identity compaction proof: state follows `lane_id` across voice removal and reordering
- Configurable `max_loop_lanes` with diagnostic logging (replaces silent clamping)
- LoopBased→LoopBased audio routing copies full multi-lane buffer
- Operator migrations to `vivid_lane_state()`: Gain, Filter, Bitcrush, LFO, Envelope
- LoopBased lane_id propagation from upstream identity-bearing spreads

### Phase B: Structural reshaping ✓

- Repeat (Structural): broadcast scalar to N lanes
- Tile (Structural): tile short pattern to target length
- Select (Reduction): pick one lane to scalar (`lane_set_id=0`)
- Compiler metadata verified: fresh `lane_set_id` on Repeat/Tile, scalar on Select
- Mismatch resolution: Select reduces one branch to scalar, enabling mixing

### Phase C: Frame-domain per-lane lifting ✓

- LoopBased dispatch in frame executor with `vivid_lane_state()` support
- Compiler Pass 4d assigns LoopBased to strategy-independent frame operators
- Identity-bearing lane_ids propagation and compaction proof on frame path
- `VividFrameContext` extended with `lane_id`, `lane_state_fn`, `allocate_lane_id_fn`, `retire_lane_id_fn`

### Phase D: Breadth proof cases ✓

- FFTAnalysis reclassified as `VIVID_LANE_STRUCTURAL`
- 256-lane and 512-lane frame LoopBased lifting verified
- FFT pipeline: `Repeat(1024)` → `FFTAnalysis(fft_size=1024)` → per-bin LoopBased processing (512 bins)

### Phase E: GPU compute-backed lane evaluation ✓

- Compute shader helpers in `gpu_common.h`
- CPU vs GPU direct comparison: same operation, same input, output matches within tolerance
- Lane ordering preserved through GPU dispatch
- Reduction on GPU: sum 128 lanes to scalar, matches CPU

### Phase F: Polish and naming ✓

- Transport naming: `VIVID_PORT_SPREAD` → `VIVID_PORT_LANE_ARRAY`, `VividSpreadPort` → `VividLanePort`, `input_spreads` → `input_lanes`, etc. Clean break, no aliases.
- Formal planner boundary: `AudioLanePlan` / `FrameLanePlan` structs, extracted `plan_audio_lane_strategy()` and `plan_frame_lane_strategy()` from inline compiler logic.

---

## Remaining future work

### Phase 7: UI and authoring

- Lane-count badges on wires and ports
- Visual distinction for structural and reduction nodes
- Operator scaffold templates with lane behavior declarations
- Opdev documentation updated to lane vocabulary

### GPU visual proof cases

- FFT spectrum → GPU instanced shapes (lane-bearing data → per-instance rendering)
- Structural particle emitter → per-particle GPU integration
- Requires GPU operators that consume lane-bearing data for rendering (not yet implemented)

### Stronger GPU backend proof

- One shared semantic operator with both CPU LoopBased and GPU compute implementations, compared through the runtime (not standalone)
- Identity-bearing and reduction coverage on the GPU compute path

### vivid-wavetable migration

- External package needs updating for the `VIVID_PORT_LANE_ARRAY` / `input_lanes` rename
- No backward-compat aliases in core headers — package must be rebuilt against current API
