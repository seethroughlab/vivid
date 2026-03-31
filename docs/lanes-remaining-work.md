# Lanes Remaining Work — Phased Plan

## Inventory

11 items documented as future work across the three lane architecture docs. Grouped into 6 phases by dependency and value.

The remaining work falls into two categories:

- proving backend equivalence
- removing interim runtime constraints that still prevent the execution model from being fully general

---

## Phase A: Prove the execution model

**Goal:** Validate that `InstancePerLane` and `LoopBased` produce identical results, migrate enough operators to make the model real, and resolve the biggest remaining runtime constraint that keeps `LoopBased` from being a generally valid backend.

**Why first:** Everything else builds on confidence that the execution strategy alignment actually works. Right now the `LoopBased` path exists but still lacks both runtime equivalence proof and a fully general runtime shape. See [lanes-execution-strategy-alignment.md](lanes-execution-strategy-alignment.md) for why backend independence is the target and why the current bounded/intermediate runtime shape is not yet enough.

### A1. InstancePerLane ↔ LoopBased equivalence test
- Run `LaneSlewOp` under both strategies with identical 4-lane input
- Compare audio output sample-by-sample within tolerance
- Requires wiring `LaneSlewOp` in both a static stereo graph (`InstancePerLane`) and a structural spread graph (`LoopBased`)

### A2. End-to-end identity compaction test
- `PolyVoiceAllocator` creates 4 voices, releases voice 2, compacts to 3
- Downstream oscillator (using `vivid_lane_state`) must prove that voice 1 and voice 3 state survived with correct `lane_id`, not positional index
- Requires a test harness that can inspect per-lane state after compaction

### A3. Convert LFO and Envelope to lane-aware
- Move per-element state to `vivid_lane_state()`
- Declare `kStrategyIndependent = true`
- These replace the deleted `SpreadLFO` / `SpreadADSR` with ordinary operators that work under any execution strategy
- Test: `LFO` receiving multi-lane gate input produces per-lane independent output

### A4. Migrate 2-3 core audio operators to `vivid_lane_state`
- Candidates: Gain (stateless, trivial), Filter (has per-voice spread indexing), Bitcrush (has counter state)
- For each: move per-lane state to `vivid_lane_state()`, declare `kStrategyIndependent`
- Test: stereo processing still works (now via LoopBased-compatible path even if still evaluated as `InstancePerLane`)

### A5. Remove or formalize the current LoopBased lane-capacity limit
- The current `LoopBased` audio path is not yet fully general
- It currently clamps runtime lanes to a fixed bound (`kMaxLoopLanes = 16`)
- It also preallocates `LoopBased` buffers to that same fixed capacity
- This limit must be resolved before the execution model can be considered generally valid

Acceptable outcomes:
- preferred: remove the fixed limit and support runtime-dynamic lane counts safely
- acceptable intermediate step: make the limit explicit, configurable, and tested, with clear failure behavior when exceeded

Verification:
- test with lane counts above the current fixed bound
- verify behavior is either:
  - correct and uncapped, or
  - explicitly rejected with deterministic, documented behavior
- do not allow silent truncation or silent clamping as the final state

**Scope:** ~700 LOC. **Risk:** Medium — touches core audio operators and the current LoopBased runtime shape.

---

## Phase B: Structural reshaping

**Goal:** Unlock N→M lane transformations with explicit structural operators.

**Why second:** Currently, mismatched non-scalar lane sets fail compilation. Users need reshape operators to build non-trivial graphs with multiple lane sources.

### B1. Implement core reshape operators
At minimum:
- **Repeat** — broadcast a value to N lanes (explicit version of scalar broadcast)
- **Tile** — repeat a short lane set to fill a longer one (3 → 12 by tiling)
- **Select** — pick one lane from a multi-lane set (explicit reduction to scalar)

Optional but valuable:
- **Zip** — interleave two lane sets
- **Flatten** — concatenate lane sets

Each declares `kLaneBehavior = VIVID_LANE_STRUCTURAL` with appropriate lane-set effect (new ID, reshaped count).

### B2. Tests
- `Repeat(scalar, count=8)` → pointwise op → verify 8-lane output
- `Tile([1,2,3], count=9)` → verify `[1,2,3,1,2,3,1,2,3]`
- `Select(lane=2, input=[a,b,c,d])` → verify scalar `c`
- mismatched lane sets → `Tile` → now legal

**Scope:** ~400 LOC. **Risk:** Low — new operators, no runtime changes.

---

## Phase C: Frame-domain per-lane lifting

**Goal:** Extend lane lifting to frame-rate operators so they can be auto-lifted like audio operators.

**Why third:** Completes the "one model" story — frame and audio operators both get lifted transparently.

### C1. Add LoopBased/InstancePerLane support to FrameExecutor
- Similar to audio executor's dispatch paths
- Frame operators with `kStrategyIndependent` and structural upstream get `LoopBased`
- Frame operators with static multi-lane input get `InstancePerLane` (if that case exists)

### C2. FrameLiftGroup (if needed)
- May reuse the same pattern as `LaneLiftGroup` but for frame-rate nodes
- Or the `LoopBased` path may be sufficient (frame operators are cheaper, loop overhead is negligible)

### C3. Test
- `SpreadSourceOp` → frame-rate pointwise op (with `kStrategyIndependent`) → verify per-lane output
- Currently this runs once per tick seeing the full spread; after C1 it runs per-lane

**Scope:** ~300 LOC. **Risk:** Medium — new frame executor path.

---

## Phase D: Proof cases for breadth

**Goal:** Prove the lane model handles the full range of use cases from the architecture doc's acceptance scenarios.

### D1. FFT-driven visual instancing
- `FFTAnalysis` (Structural, frame-cadence) → pointwise color/size mapping → `InstancedShapes`
- All using lane provenance from the same FFT lane set
- Validates positional lane sets (no identity needed) at scale (512 bins)

### D2. Particle/instance systems
- Structural particle emitter → pointwise position/velocity integration → GPU instancing
- Validates large lane counts (100+) with frame-rate lane lifting

**Scope:** ~500 LOC. **Risk:** Low — application-level, no runtime changes.

---

## Phase E: GPU compute-backed lane evaluation

**Goal:** Prove that backend choice is genuinely an implementation detail by running lanes through WGSL compute shaders.

### E1. Compute shader infrastructure
- Add WGSL compute pipeline helpers to `gpu_common.h`
- Storage buffer creation/binding
- Compute dispatch helper

### E2. Compute-backed lane operator proof case
- A simple pointwise operation (e.g., lane-parallel sine oscillator) implemented as a WGSL compute shader
- Input: lane-bearing control data (frequencies)
- Output: lane-bearing audio or control data
- Validates: lane ordering preserved, identity continuity, reduction equivalence

### E3. Backend equivalence test
- Same operator, same input, CPU `LoopBased` vs GPU compute
- Verify identical output within floating-point tolerance

**Scope:** ~800 LOC. **Risk:** High — new GPU infrastructure.

---

## Phase F: Polish and naming

**Goal:** Clean up naming residue and formalize the planner boundary.

### F1. Transport naming alignment
- Rename `VIVID_PORT_SPREAD` → `VIVID_PORT_LANE_ARRAY` (or similar)
- Rename `input_spreads` / `output_spreads` → `input_lanes` / `output_lanes` on contexts
- Rename `VividSpreadPort` → `VividLanePort`
- Mechanical — touches every operator, every test, every doc reference
- Should be done in one atomic pass

### F2. Formal runtime planner boundary
- Currently the "planner" is Pass 4c with hardcoded rules
- That is an intermediate mechanism, not the final architectural boundary
- A formal planner boundary is required before backend choice can honestly be called an implementation detail
- The exact type name can remain open (`LaneExecutionPlan`, `LaneEvaluationStrategy`, etc.)
- This planner must become the place where backend selection is represented and reasoned about

**Scope:** F1 ~1000 LOC (mechanical). F2 TBD.

---

## Sequencing

```
A (prove execution model)     validates strategy alignment and resolves the current bounded LoopBased runtime shape
 ↓
B (reshape operators)         unlocks N→M lane transformations
 ↓
C (frame-domain lifting)      completes the "one model" story
 ↓
D (proof cases)               validates breadth (FFT, particles)
 ↓
E (GPU compute)               validates backend independence
 ↓
F (polish + naming)           cleanup and planner formalization
```

A is the most important near-term work — without it, the LoopBased path is infrastructure without proof and still carries an interim runtime constraint. B and C are independently valuable. D depends on B/C for some scenarios. E is the long-term target. F can happen anytime but is most efficient last, with the planner boundary required before the execution model can honestly be considered backend-independent.
