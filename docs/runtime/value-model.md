# Vivid Value Model — Canonical Contract

**Status:** Phases 0–3 + Phase 4a/4b/4c + Phase 5a/5b of the
[lane-value clean-break](../plans/lane-value-model-clean-break.md) are **done**. The value API is **complete**:
`VividFrameContext`/`VividGpuContext`/`VividAudioContext` `values`/`value_outputs` cover every payload (float,
STRING_LANES, texture, audio) across both cadences (frame + audio) and all execution strategies
(scalar/lifted/loopbased), backed by the existing lane/string-lane/texture/audio-buffer transport. Operators
can fully consume/produce via the value API — the precondition for Phase 6 (migrate the seed operators) and
Phase 7 (swap transport to native value storage + remove the lane API). The vocabulary (`value_model.h`) + value views (`value_view.h`) are wired into the
descriptor/codegen/probing (ABI **6**); the compiler runs a **value-flow inference pass** (Pass 2.7) computing
a `ValueEnvelope` per edge/port from `multiplicity_behavior`, proven equivalent to the lane sets
(`CompiledGraph.value_flow_mismatches == 0`); the runtime has a unified **value-storage substrate**
(`ValueBuffer`/`ValueArena`/`ValueRef` + `BridgeValueSlot` + health counters); and (Phase 4a)
**`VividFrameContext` exposes `values`/`value_outputs`** so frame/control operators can consume/produce
scalar-or-many values via the value API, backed by the existing lane transport (proven by `ValueGainOp`). All
**additive** — the lane system is **still the live execution path** (Phase 4b/5 switch execution onto the
substrate; Phase 7 removes lanes). This document is the target contract.

Phase 0 is complete when every current lane surface has an explicit new-model target here and no
naming/semantic decision is left to later phases.

---

## 1. The model

The lane system represents "many values" with special port types (`VIVID_PORT_LANE_ARRAY`,
`VIVID_PORT_STRING_LANES`), side-channel lane buffers, execution strategies, and bridge-only scratch. The
value model replaces all of that: **every runtime value carries four orthogonal properties**, and operators
**declare** how they transform multiplicity instead of relying on runtime heuristics.

| Axis | Enum (`value_model.h`) | Values | Replaces |
|------|------------------------|--------|----------|
| **Payload type** — what it is | `VividValueType` | Float, Audio, Texture, String, Custom | the payload half of the port-type enum |
| **Multiplicity** — one or many | `VividMultiplicity` | Scalar, Many | `LaneSet.lane_count == 1` vs `> 1` |
| **Identity** — how "many" is named | `VividIdentityMode` | None, Positional, StableIds | `LaneSet.identity_bearing` + scalar/positional split |
| **Storage** — where bytes live | `VividStorageKind` | Cpu, AudioBlock, Gpu, BridgeSlot, StringStore, Custom | lane buffers / string lanes / GPU promotion / bridge slots |

Plus the operator-facing declaration:

| | Enum | Values |
|---|------|--------|
| **Multiplicity behavior** — what the operator does | `VividMultiplicityBehavior` | ScalarOnly, Map, Reduce, Generate, Collect, Preserve, Kernel |

### Locked invariants

1. **Multiplicity belongs to values, not to payload-specific port types.** There is no lane-array or
   string-lanes port type. A port is a payload type that may carry Scalar or Many.
2. **Edges carry** value type + multiplicity + identity lineage + storage requirement. **Operators declare**
   the multiplicities they accept and produce (via `VividMultiplicityBehavior`).
3. **Audio channel count is audio payload layout, not value multiplicity.** A stereo audio value is *one*
   value with 2 channels — never two values. The compiler infers channel layout independently from
   multiplicity (today these are entangled in `plan_audio_lane_strategy`).
4. **Execution strategy is derived, not declared.** The runtime computes how to evaluate a node (single,
   lifted-per-element, or whole-collection) from the operator's behavior + domain. There is no public
   `LaneExecutionStrategy`.

---

## 2. Multiplicity behaviors + identity semantics

The runtime derives node evaluation from these. "Identity" = what happens to element identity tokens.

| Behavior | Shape | Identity semantics | Old equivalent |
|----------|-------|--------------------|----------------|
| **ScalarOnly** | 1 → 1 | n/a (no Many) | — |
| **Map** | Many(N) → Many(N) | **preserved** 1:1; per-element state keyed by identity | `VIVID_LANE_POINTWISE` |
| **Reduce** | Many(N) → 1 | **collapsed** — the op must explicitly choose the result's identity (usually None) | `VIVID_LANE_REDUCTION` |
| **Generate** | 1/control → Many(M) | **minted** — the op assigns fresh identity (Positional or StableIds) | `VIVID_LANE_STRUCTURAL` (expanding) |
| **Collect** | several scalars → Many(K) | **minted** Positional (input order) | `VIVID_LANE_STRUCTURAL` (gathering) |
| **Preserve** | Many(N) → Many(N) | **forwarded unchanged** (pass-through; no per-element compute) | `VIVID_LANE_POINTWISE` pass-through |
| **Kernel** | Many(N) → Many(M) | sees the **whole** collection in one invocation (cross-element / neighborhood); identity is op-defined; **cannot be element-lifted** | `VIVID_LANE_KERNEL` |

**Per-element state** (the `vivid_lane_state()` successor) is available to Map/Kernel/Generate operators over
values with `VividIdentityMode::StableIds`: state is keyed on the **identity token**, not a node index, so it
survives reorder, compaction, **and recompile**.

### Open decisions (confirm at Phase-0 sign-off)
- **Kernel is kept as a 7th behavior** (not folded into Map/Reduce). Rationale: a cross-element operator must
  not be element-lifted; the executor needs to distinguish "see all elements at once" from "per-element."
  The clean-break doc lists six behaviors; this adds Kernel as the honest home for the old
  `VIVID_LANE_KERNEL`. *Alternative:* express kernels as `Reduce`→`Generate` composition (rejected: loses
  per-op locality + identity continuity).
- **IdentityMode is 3-valued** (None / Positional / StableIds) rather than a 2-valued has-identity bool —
  needed to distinguish positional Many (index-meaningful, no stable token) from voice-like Many.

---

## 3. Old → New mapping (complete)

Every current lane surface (from the Phase-0 catalog) maps to a value-model target. Source file:line are the
current locations to be replaced.

### Operator API (`src/operator_api/`)
| Old surface | File:line | New target |
|-------------|-----------|------------|
| `VIVID_PORT_LANE_ARRAY` | `types.h:66` | payload `Float` + `Many` on the value envelope |
| `VIVID_PORT_STRING_LANES` | `types.h:68` | payload `String` + `Many` |
| `VIVID_PORT_TRANSPORT_LANE_ARRAY` / `_STRING_LANES` | `types.h` | storage kind + multiplicity (transport derived) |
| `VividLaneView` / `VividLaneOutput` | `types.h:286-297` | `VividValueView` / `VividValueOutput` + typed float helpers |
| `VividStringLaneView` / `VividStringLaneOutput` | `types.h:299-311` | same value view/output, String payload |
| `VividFrameContext.{input,output}_lanes`, `..._string_lanes` | `types.h:402-411` | unified `values[]` views/outputs on the context |
| `VividAudioContext.{input,output}_lanes` | `types.h:347-348` | unified value views/outputs |
| ctx `lane_count/lane_index/lane_set_id/lane_id` | `types.h:363-366,418-421` | value-envelope `multiplicity/value_count/identity_mode` + per-invocation element index/token |
| `VividLaneBehavior` {POINTWISE,STRUCTURAL,REDUCTION,KERNEL} + `strategy_independent` | `types.h:24-28; desc 187-188` | `VividMultiplicityBehavior` {Map, Generate/Collect, Reduce, Kernel, …} |
| `vivid_lane_state(ctx, lane_id, T)` | `operator.h:526` | identity-keyed value state under `StableIds` (token-keyed, recompile-stable) |
| `has_lane_behavior` / `get_lane_behavior` SFINAE | `operator.h:411-434` | `has_/get_multiplicity_behavior` |
| entry points `vivid_create/process_frame/audio/gpu/destroy/...` | `operator.h:573-695` | **unified registration contract** at the Phase-1 ABI bump (see §5) |
| `VIVID_OPERATOR_ABI_VERSION = 5` | `types.h:11` | bumped in Phase 1 |

### Compiler (`src/runtime/graph/`)
| Old surface | File:line | New target |
|-------------|-----------|------------|
| Pass 2.6 lane-set planning | `graph_compiler.cpp:554-705` | **value-flow pass** (type + multiplicity + identity lineage + storage inference) |
| `LaneSet {lane_set_id, lane_count, identity_bearing}` | `lane_types.h:22-28` | value envelope on edges {value_type, multiplicity, value_count, identity_mode + lineage} |
| `next_lane_set_id` | `compiled_graph.h:582` | value lineage id allocator |
| `LaneExecutionStrategy` {Scalar,InstancePerLane,LoopBased} | `lane_types.h:35-40` | **derived** node eval mode (private); removed as a public concept |
| `plan_audio_lane_strategy` / `plan_frame_lane_strategy` | `graph_compiler_planning.cpp:41-126` | value-eval-mode derivation; **audio channel negotiation split out** (invariant 3) |
| `AudioNodeState.{execution_strategy,lane_lift_count,lane_lift_set_id,lane_id_port}` | `compiled_graph.h:221-225` | derived eval-mode metadata |
| `CompiledNode.{frame_execution_strategy,frame_lane_id_port}` | `compiled_graph.h:516-518` | derived eval-mode metadata |
| `CompiledEdge.{lane_set_id,lane_count}` | `compiled_graph.h:157-158` | edge value envelope |
| `CompiledNode.{input,output}_lane_refs` (`LaneBufferRef`) | `compiled_graph.h:425-427` | `ValueRef` (Phase 3) |
| `CompiledNode.{input,output}_lane_sets` | `compiled_graph.h:513-514` | per-port value envelope |
| `kGpuLanePromotionThreshold` / `plan_gpu_lane_promotion` / `lane_input_gpu_promoted` | `graph_compiler_internal.h:14; planning.cpp:210; compiled_graph.h:368` | `VividStorageKind::Gpu` as a storage policy decision |
| `kDefaultLaneCapacity = 1024` / `BridgeLaneSlot` / `warn_oversized_bridge` | `graph_compiler_internal.h:13; snapshot_types.h:21-26; graph_compiler.cpp:980-1001` | fixed-capacity **bridge value slots** + explicit bounded overflow (**folds 01-R2-F7**) |
| lane-mismatch hard error (Pointwise) | `graph_compiler.cpp:614-621` | multiplicity-incompatibility diagnostic |
| `input_string_lanes`/`output_string_lanes` + string-lane views/bufs | `compiled_graph.h:435-445` | unified value storage (String + Many) — parallel string path removed |

### Executor + bridge (`src/runtime/graph/`, `src/runtime/audio/`)
| Old surface | File:line | New target |
|-------------|-----------|------------|
| frame lane normalize / `process_loopbased_node` / scalar→lane lift | `frame_executor.cpp:108-138,356-485,708-807` | value preparation + Map/Generate/Collect/Reduce/Kernel paths |
| `lane_pool_` (growable) | `frame_executor.h:133` | frame value arena |
| audio lane lift groups / `process_lifted_audio_node` | `audio_executor.cpp:131-181; .h:132` | precomputed value slots; per-element eval w/o RT allocation |
| `LaneStateService` (`allocate/get/retire/sweep`, `(node_idx,lane_id)` key) | `lane_state.h:28-162` | identity-token-keyed value-state service (node-index-independent) |
| `BridgeLaneSlot` storage + `lane_overflow_count_` | `snapshot_types.h:21-26; audio_frame_bridge.{h,cpp}` | bridge **value** slots + overflow/identity-truncation counters |
| `ParamSnapshot.lane_inputs` / `AnalysisSnapshot.lane_outputs` | `snapshot_types.h:45,58` | bridge value-slot arrays |
| GPU lane buffers (`input_lane_gpu_buffers/lengths/count`) | `frame_executor.cpp:535-557` | GPU-storage values |
| `graph_snapshot_builder` lane fields (`lane_set_id`, `lane_count` on Connection/Node snapshots) | `graph_snapshot_builder.cpp:404-435,485-505` | value-envelope fields for UI (multiplicity, value_count, identity_mode) |

### Folded audit findings (per [[project_lane_value_clean_break]])
- **01-R2-F7** — the bridge `>1024` clamp / `lane_overflow_count_` becomes the value-bridge-slot capacity +
  explicit bounded overflow + visible counters (Phase 3/5).
- **Round-1 lane-state-across-recompile gap** — the catalog confirms `LaneStateService` identity breaks when
  node indices change on recompile; `VividIdentityMode::StableIds` keys state on the identity token, not the
  node index, fixing it (Phase 1 contract, Phase 3/5 runtime).
- **02-R2-F3** — the four+ separate `extern "C"` entry points (codegen-only; builtins locked to them) are
  unified into one registration contract at the Phase-1 ABI bump.

---

## 4. Migration checklist (later phases)

- **Seed operators (≈128)** in `operators/{audio,control,gpu,shared}` → new value API (Phase 6).
- **Sibling repos (≈75)** → fail loud at descriptor/probe with actionable ABI/schema messages until migrated.
- **Demo graphs** (`graphs/**.json`) → payload type + multiplicity metadata (Phase 6).
- **MCP / control-server** graph-mutation tools → no lane-specific ports / names (Phase 6).
- **UI** → render multiplicity independently of payload domain; inspector shows value count / identity mode /
  storage (Phase 6). *(Lane-legibility rendering already reads provenance via the snapshot — it maps onto the
  value envelope cleanly.)*
- **Package scaffolding** templates → emit the new API only (Phase 1/6).
- **Docs** → `docs/runtime/graph.md`, operator-authoring, package, LLM-integration; lane docs → migration
  notes (Phase 6/7).

## 5. Phase-1 entry point (next, not now)
Phase 1 wires `value_model.h` into descriptors/contexts/codegen, adds `VividValueView`/`VividValueOutput` +
the multiplicity-acceptance descriptor fields, **unifies the registration contract** (02-R2-F3), **bumps
`VIVID_OPERATOR_ABI_VERSION`** so stale operators are rejected, and adds compile examples for each behavior.
Phase 1 is the engine-destabilization point — gated on sign-off of this document.

---

## Old→new vocabulary cross-reference (quick)
lane → value · lane array / string lanes → Many · lane_set_id / lane_count → multiplicity + value_count +
identity lineage · identity_bearing → IdentityMode::StableIds · lane execution strategy → derived eval mode ·
lane buffer → value storage · GPU lane promotion → StorageKind::Gpu · bridge lane slot → bridge value slot ·
`vivid_lane_state` → identity-keyed value state · LaneBehavior → MultiplicityBehavior.
