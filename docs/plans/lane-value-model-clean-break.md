# Clean-Break Lane System Redesign Plan

> **STATUS: ✅ COMPLETE (2026-06-09).** All phases (0–8) are implemented, merged to `master` (`93e6b27c`),
> and pushed. The value model is the sole multiplicity authority at operator ABI 10; the entire lane stack
> (lane port types, the lane C-API, lane buffers, Pass-2.6 lane-sets, and `VividLaneBehavior`) is removed.
> The downstream sibling-package ABI-10 migration (vivid-3d/ml/plexus/physics2d/wavetable/glitch) is tracked
> separately and is also complete.

## Summary

Replace the current lane system with a first-class value and multiplicity model. In the new system, "many values" are not represented by special port types, side-channel lane buffers, bridge-only scratch state, or execution-strategy glue. Instead, every runtime value carries:

- payload type: float, audio, texture, string, or custom
- multiplicity: scalar or many
- identity metadata: none, positional, or stable IDs
- storage policy: CPU, GPU, audio block, bridge slot, string storage, or custom storage

This is a clean break from the old system. The implementation should remove the old lane API rather than shim it. Old package operators, old graph lane surfaces, and old lane-oriented runtime paths should fail loudly until migrated.

The goal is to make multiplicity a general property of values across all domains, not a separate transport model that only some payloads understand.

## Public Interface Changes

Replace lane-specific public surfaces with payload type plus multiplicity metadata.

- Remove public dependency on `VIVID_PORT_LANE_ARRAY` and `VIVID_PORT_STRING_LANES`.
- Remove old lane-specific operator context surfaces such as `VividLaneView`, `VividLaneOutput`, `VividStringLaneView`, and `VividStringLaneOutput`.
- Introduce value views and output builders with a shared envelope, for example `VividValueView` and `VividValueOutput`, plus typed helpers for float, audio, texture, string, and custom payloads.
- Add explicit value metadata fields for payload type, multiplicity, identity mode, lane/value count, storage kind, and flags.
- Treat audio channel count as audio payload layout, not value multiplicity.

Operators should declare multiplicity behavior directly instead of relying on runtime lane heuristics.

- `ScalarOnly`: accepts one value and emits one value.
- `Map`: applies the same operation per element and preserves identity.
- `Reduce`: consumes many values and emits one value.
- `Generate`: emits many values from scalar/control input.
- `Collect`: aggregates several scalar inputs into one many-valued output.
- `Preserve`: forwards multiplicity and identity unchanged where applicable.

The operator ABI and graph schema should be bumped as part of the transition. Old operators and old graph lane surfaces should be rejected with actionable diagnostics rather than silently adapted.

## Phased Implementation

### Phase 0: Design Lock — ✅ DONE 2026-06-05

**Deliverables:** the canonical contract [`docs/runtime/value-model.md`](../runtime/value-model.md) (4 value
axes + 6→7 multiplicity behaviors + locked enum names + identity semantics + the **complete old→new mapping
table** from a full catalog of every current lane surface + the migration checklist) and the **inert**
vocabulary header `src/operator_api/value_model.h` (locked enums; `#include`d by nothing yet — Phase 1 wires
it). No execution-code change; build green.

**Audit findings folded in** (per `project_lane_value_clean_break` memory): **01-R2-F7** (bridge >1024 clamp →
bridge value slots + explicit bounded overflow), the **round-1 lane-state-across-recompile gap** (→
`VividIdentityMode::StableIds` keys state on the identity token, not the node index), and **02-R2-F3** (→
unify the operator registration contract at the Phase-1 ABI bump; `VIVID_OPERATOR_ABI_VERSION` is currently
5). Two open decisions surfaced for sign-off: keep **Kernel** as a 7th behavior (don't fold into Map/Reduce);
make **IdentityMode 3-valued** (None/Positional/StableIds).

Original Phase-0 spec (now satisfied by the above):

Write the new value/multiplicity contract before changing execution code.

- Define the canonical vocabulary in `src/operator_api` and the runtime docs.
- Decide exact enum names for value type, multiplicity, identity mode, storage kind, and operator multiplicity behavior.
- Document the invariant that multiplicity belongs to values, not to payload-specific port types.
- Document that graph edges carry value type plus multiplicity, while operators declare what multiplicities they accept and produce.
- Document how identity behaves for map, reduce, generate, collect, and preserve operations.
- Add a migration checklist for seed operators, demo graphs, MCP tools, UI inspection, package scaffolding, and docs.

Phase 0 is complete when the old-to-new concept mapping is explicit and no naming or semantic decisions are left to later phases.

### Phase 1: New API Skeleton — ✅ DONE 2026-06-05 (additive; engine unaffected)

**Done:** `value_view.h` (`VividValueView`/`VividValueOutput` + typed helpers); descriptor fields
(`VividOperatorDescriptor.multiplicity_behavior`, `VividPortDescriptor.{value_type,multiplicity}`);
`VIVID_OPERATOR_ABI_VERSION` bumped **5→6**; codegen emits `multiplicity_behavior` via SFINAE
`get_multiplicity_behavior<T>()` (default-derived from `lane_behavior`, overridable with
`kMultiplicityBehavior`); probing surfaces the fields (MCP `operator_docs`/`list_types`); descriptor
validation gained `kInvalidMultiplicityBehavior`; tests `test_value_model` (override + derivation
static-asserts + value_view) and the extended `test_operator_docs_metadata` (probing JSON) pass.

**Additive & verified non-disruptive:** the old lane API + lane execution are untouched (removed in Phase 7);
the ~152 seed operators auto-rebuilt to ABI 6 and still run via lanes (`test_demo_graphs` +
`test_lane_equivalence` + the full sanity sweep pass). Only external sibling dylibs (ABI 5) now reject at
probe with a clean `abi_mismatch` diagnostic. **02-R2-F3 registration-contract unification was DEFERRED** to a
focused follow-up (avoid compounding ABI-bump risk). Full 7-behavior example set + `values[]` context wiring
deferred to Phases 4/6.

Original Phase-1 spec:

Introduce the new public API and descriptor shape before wiring it into runtime execution.

- Add new public value view/output structs in `src/operator_api`.
- Add new descriptor fields for payload type and multiplicity acceptance.
- Add operator multiplicity behavior declarations to descriptors.
- Update descriptor probing and `operator_codegen` so generated boundaries expose the new contract.
- Bump the operator ABI so stale old operators are rejected.
- Add compile examples for scalar, map, reduce, generate, collect, preserve, and string-many operators.

Phase 1 is complete when new descriptors are visible through registry/probing, new API examples compile, and old operators fail for the right reason once the ABI bump is active.

### Phase 2: Compiler Rewrite — ✅ DONE 2026-06-05 (additive value-flow pass)

**Done:** added **Pass 2.7 — value-flow inference** (`plan_value_flow` in `graph_compiler_planning.cpp`),
running in parallel with Pass 2.6. Computes a `ValueEnvelope` (value_type · multiplicity · identity ·
storage) per edge/port from each operator's `multiplicity_behavior` (P1) — **independently of lane ids**, and
treating **audio channel count as payload layout, not multiplicity** (the Phase-2 separation invariant). Per
edge it asserts the inferred multiplicity is **equivalent** to the Pass-2.6 lane sets and records mismatches
in `CompiledGraph.value_flow_mismatches` (non-fatal; tests assert 0). The equivalence harness immediately
caught (and I fixed) a real inference bug — `value_flow_mismatches == 0` now holds across every lane test
(structural/reduction/multi-channel + cross-cadence bridge) + demo graphs.

**Additive & verified:** lane planning, lane execution, the strategy planners, and all lane consumers are
**untouched** — the envelopes are not yet consumed by execution (Phases 4-5 switch onto them). All lane +
graph tests green; `test_value_flow` added. **Deferred to 2b/later:** surfacing the envelope on the UI
snapshot; deriving the lane fields from the envelope / removing `LaneExecutionStrategy` (the Phase-4/5
convergence, then Phase-7 removal).

Original Phase-2 spec:

Replace lane propagation with value type and multiplicity inference.

- Replace lane-set planning with a value-flow pass.
- Make compiled edges carry payload type, multiplicity, identity lineage, and storage requirements.
- Derive node evaluation mode from operator multiplicity behavior and domain constraints.
- Remove lane execution strategy as a public planning concept.
- Separate audio channel negotiation from value multiplicity inference.
- Emit compile diagnostics for incompatible multiplicity, identity loss, unsupported reductions, and bridge capacity risks.

Phase 2 is complete when the compiler can plan mixed scalar/many graphs without using `VIVID_PORT_LANE_ARRAY`, and audio channel count inference is independent from multiplicity.

### Phase 3: Unified Runtime Storage — ✅ DONE 2026-06-05 (additive substrate)

**Done:** the value-storage substrate as additive, unit-tested infrastructure — `ValueBuffer` (one
payload-tagged buffer: float/string/custom CPU storage carrying the Phase-2 `ValueEnvelope`, subsuming
`LaneBuffer`/`StringLaneBuffer`/custom-snapshot), `ValueRef` (RAII intrusive ref), `ValueArena` (growable
frame / fixed audio pool, mirroring `LaneBufferPool`), `ValueHealthCounters`
{bridge_overflow,identity_truncation,dropped_many}, and `BridgeValueSlot` (envelope across the cadence
boundary + RT-safe `write_clamped` folding **01-R2-F7**). RT-safety preserved: fixed (audio) buffers/arenas
return false/null on overflow with **no allocation**; only frame/non-pool storage grows. `test_value_buffer`
covers scalar+many per payload + the RT-safety + overflow accounting.

**Additive & verified:** lane storage, executors, and the bridge are **untouched** (the substrate is not yet
consumed by execution — Phases 4–5 wire it). All value/lane/bridge/audio/graph tests green. **Deferred to
4–5/7:** wiring the executors+bridge onto the substrate; unifying GPU/audio-block storage into `ValueBuffer`
(stay handle-based); removing `LaneBuffer`/`StringLaneBuffer`/`BridgeLaneSlot` (Phase 7).

Original Phase-3 spec:

Replace lane-buffer-centered transport with general value storage.

- Add runtime-internal `ValueRef` and `ValueBuffer` storage for float, audio, texture, string, and custom payloads.
- Add per-domain arenas or pools for frame, audio, GPU, and bridge ownership.
- Replace bridge lane slots with fixed-capacity bridge value slots.
- Preserve real-time safety: audio execution must not allocate, grow vectors, lock, or perform descriptor lookup.
- Move GPU promotion to value storage policy instead of lane-specific special handling.
- Add runtime health counters for bridge overflow, identity truncation, and dropped many-valued payloads.

Phase 3 is complete when frame/audio/bridge storage can represent scalar and many values uniformly, and no runtime path needs separate float-lane and string-lane storage concepts.

### Phase 4: Frame Executor Migration

**Phase 4a — ✅ DONE 2026-06-05 (frame value-view API, additive).** First execution-touching phase, kept
additive + low-risk. `VividFrameContext` now exposes `values` (`VividValueView`) + `value_outputs`
(`VividValueOutput`) — the value-model API — so control operators *can* consume/produce scalar-or-many values.
In 4a the FLOAT value output is backed by the SAME `out_lane_bufs` LaneBuffer the lane path uses (via
`value_output_adapter.h`), so a value-API operator's output flows downstream through the unchanged lane
propagation and lane-API + value-API operators interoperate. Input value views alias the lane input data +
the Pass-2.7 envelope. Proven by `ValueGainOp` (kMultiplicityBehavior=Map) + `test_frame_value_api` running
through real frame execution (scalar + many). The lane path, all ~152 operators, and behavior are unchanged;
23 value/lane/frame/graph/demo tests green.

**Phase 4b — ✅ DONE 2026-06-05 (frame value API, many-string).** Extends 4a to the `STRING_LANES` payload —
the clean mirror of float lanes, backed by the existing `StringLaneBuffer` transport
(`make_string_value_output()`; per-output-port backing chosen by port type in `graph_compiler_init`; the
per-tick value-view input staging now points string ports at `in_string_lane_ptrs`). `StringValueEchoOp`
(kMultiplicityBehavior=Map) + `test_frame_value_api_string` prove the value API round-trips many strings
through real frame execution and interoperates with string-lane neighbors. Float (4a) + many-string (4b) both
flow through the value API; engine unchanged; 23 + 3 tests green.

**Phase 4c — ✅ DONE 2026-06-05 (GPU value routing).** Extends the value API to the GPU/texture domain:
`VividGpuContext` exposes `values`/`value_outputs`. Texture inputs become `VividValueView` carrying the
resolved WGPUTextureView handle (`value_type=TEXTURE`, scalar, storage=GPU); float/string ports keep the
4a/4b views; the primary texture output is a `VividValueOutput` whose `resize()` returns the runtime render
target (`make_texture_value_output`, commit no-op). `GpuValueFillOp` + headless `test_gpu_value_api` prove the
texture OUTPUT (readback RED) + texture INPUT value view + interop (fill_b detects fill_a's texture via
`ctx->values`, renders GREEN). 18 gpu/lane/frame/value tests green; GPU path + all operators unchanged. The
frame value API now spans float + many-string + texture/GPU.

**Deferred to 4d/5/6/7:** scalar `VIVID_PORT_STRING` value I/O (the `output_string_values` path) + custom
value outputs (a `ValueArena` `ValueBuffer` — Phase 3's substrate gets its execution consumer); aux/multi-
texture + many-texture multiplicity; behavior-specific executor paths (Map/Reduce/Generate/Collect as distinct
logic; identity through map/preserve — 4a-c run through the existing strategy/exec path); audio executor +
bridge (Phase 5); migrating the seed operators (Phase 6); removing `input_lane_refs`/lane views (Phase 7).

Original Phase-4 spec:

Move frame, control, and GPU execution onto the new value model.

- Replace frame executor lane normalization with value preparation.
- Pass new value views to operators.
- Implement map, reduce, generate, collect, and preserve execution paths.
- Preserve identity through map and preserve operators.
- Require explicit identity behavior for reductions and collections.
- Update GPU value routing so many textures and many float controls use the same multiplicity path.

Phase 4 is complete when control and GPU operators can consume and produce scalar or many values through the new API, scalar graphs still behave the same after operator migration, and frame execution no longer depends on `input_lane_refs`, `output_lane_refs`, or lane set IDs.

### Phase 5: Audio Executor And Bridge Migration

**Phase 5a — ✅ DONE 2026-06-06 (audio value API, Scalar path).** `VividAudioContext` exposes
`values`/`value_outputs` — audio operators can read audio + control inputs and write the audio output via the
value API, backed by the existing audio-buffer transport (RT-safe, additive). `make_audio_value_output`
(provided-buffer model, like textures: `resize` returns the runtime block, `commit` no-op). The Scalar audio
path (`process_normal_audio_node`) populates the value views per block (pre-allocated fields only — no
alloc/lock). `AudioGainValueOp` + offline `test_audio_value_api` drive the real audio executor (mono 0.5 →
×2 → 1.0 on the Scalar path). The value API now spans every payload across **both cadences** (frame + audio)
for the scalar path. 10 audio/lane/frame/gpu/value tests green; audio path + all operators unchanged.

**Phase 5b — ✅ DONE 2026-06-06 (audio value API, lifted/loopbased).** Factors 5a's population into a shared
RT-safe `populate_audio_value_views(ctx, cn)` called in all three audio paths (Scalar/InstancePerLane/
LoopBased), so polyphonic/per-lane audio operators get per-lane value views (the executor's Map over the Many
input → per-lane Scalar view). `test_audio_value_api` extended with the InstancePerLane case (4ch source →
mono value-API gain lifted to 4 → per-lane `(c+1)*0.1*2`). 17 tests green. **The value API is now COMPLETE**
— every payload (float/string/texture/audio), both cadences (frame/audio), all strategies
(scalar/lifted/loopbased) — the precondition for Phase 6.

**Deferred to Phase 7:** the `BridgeValueSlot`/`ValueArena` native cross-cadence transport (the first real
`ValueArena` execution consumer) — lands at the removal/convergence when execution consumes value storage
instead of lane buffers; additive now would be dead storage.

Original Phase-5 spec:

Move audio execution and cross-cadence transport to the same value model.

- Replace audio lane metadata with value views plus audio payload layout.
- Keep channel count negotiation separate from multiplicity.
- Pre-plan all audio value slots at compile time.
- Ensure map-style audio operators can run per value without runtime allocation.
- Ensure reductions from many audio/control values into audio-rate control are explicit.
- Replace audio bridge lane slots with value bridge slots.
- Add tests for dynamic bridge overflow and recovery.

Phase 5 is complete when the audio callback uses precomputed value slots only, audio channel layout and multiplicity cannot be confused by compiler or operator API, and bridge overflow produces bounded behavior plus visible diagnostics.

### Phase 6: Operator, Graph, UI, MCP, And Docs Migration

**Phase 6 — ✅ COMPLETE 2026-06-06.** Operator code: 25/25 lane-using operators migrated. Non-code authoring
surface (`7c5d13c4`): scaffolding (operator_creator.cpp) + authoring docs (opdev_docs core/control/audio/gpu/
advanced, AGENTS.md, ARCHITECTURE.md §5.9) now teach the value API as canonical + mark the lane API legacy.
**Graph JSON: no change** (type-agnostic — node ids + port-name connections only). **UI/MCP value-rendering:
deferred to Phase 7** (lane rendering/inspection is accurate + equivalent until lanes are removed; replacing it
belongs with the removal). 25/25 operator code + The final 4 (filter, sequencer,
note_breakout, drum_sequencer) landed after closing a **Phase-5b gap** (`098081bb`): audio value views now
carry bridged `LANE_ARRAY` inputs (not just scalar `input_buffers`). note_breakout/drum_sequencer migrated via
a value-API `emit_voice_breakouts` overload (shared helper, not per-op rewrites; drum's `out_spreads` was a
vestigial unused param). **filter + sequencer also fixed a pre-existing latent indexing bug** — they read
`input_lanes[0..]` (lane-ordinal) but views are full-ordinal, so their per-lane features (filter cutoff_mod/
keytrack; sequencer external arrays) were dead; now they read the correct ports (`ctx->values[3..]`) and
function as designed (behavior change toward correctness; no test/demo regressed). `0628b30d`.

Earlier (21/25) scope insight:
Phase 7 only removes the *lane-specific* surfaces (`VividLaneView`/`VividLaneOutput`, `VIVID_PORT_LANE_ARRAY`/
`STRING_LANES`, lane sets, `LaneExecutionStrategy`, lane bridge slots) — NOT the typed accessors
(`input_values`/`input_buffers`/`input_textures`). So only the ~25 operators that *use the lane API* must
migrate; pure scalar/audio/texture ops already fit. The migration swaps lane-surface I/O calls →
`ctx->values`/`value_outputs` (helpers in `value_view.h`), keeping ports + behavior (value_outputs[p] and
output_lanes[p] share `out_lane_bufs[p]`; `ctx->values` aliases the same data) — behavior-identical.

Done via a 25-agent workflow (`f18ae732`) + per-op build/regression verification. **Migrated (21):**
dual_filter, fm_synth, sampler, slicer, sp404; alternate, envelope, euclidean, fft_analysis, folder_list,
pat_transform, pattern_seq, repeat, select, spread_noise, stack, string_select, tile;
instances_from_lanes_2d, metaball, shape_field. Also fixed a 4a gap the regression caught: placeholder nodes
weren't getting value-view staging → crash (`f3110684`).

**DEFERRED (4) + two gaps to close first:**
- **note_breakout, drum_sequencer** (voice-breakout ops): migrate the shared `emit_voice_breakouts` helper
  (`operators/shared/sequencer/voice_breakouts.h`) to a value-API version, then update all callers (incl. the
  synths) — a coordinated change, NOT per-op rewrites (the agents' rewrites segfaulted, reverted).
- **filter, sequencer** (audio ops reading `VIVID_PORT_LANE_ARRAY` inputs via the cross-cadence bridge):
  blocked by a **Phase-5b gap** — `populate_audio_value_views` aliases every audio input to `input_buffers[p]`
  (scalar), so bridged lane-array inputs (`c_in_lane_views`) aren't carried into `ctx->values`. Close that
  (audio value views carry lane-array inputs) before migrating these.
- Remaining Phase-6 surfaces (graph JSON, UI multiplicity rendering, MCP, docs) still to do.

Original spec — Migrate the product surface in one coordinated sweep.

- Update seed operators in `operators/audio`, `operators/control`, `operators/gpu`, and `operators/shared`.
- Update scaffolding templates so newly generated operators use the new API only.
- Update checked-in graph JSON descriptors and demo graphs to use payload type plus multiplicity metadata.
- Update UI port rendering to show multiplicity independently from payload domain.
- Update inspector views to display value count, identity mode, and storage kind.
- Update MCP and control-server graph mutation tools so they no longer create lane-specific ports or assume old lane names.
- Update runtime graph docs, operator authoring docs, package docs, and LLM integration docs.

Phase 6 is complete when all checked-in operators and graphs use the new model, UI and MCP can create/inspect/connect/mutate many-valued ports, and no new user-facing path emits old lane terminology except migration notes.

### Phase 7: Legacy Removal

Delete the old lane system rather than maintaining a compatibility layer.

- Remove `VIVID_PORT_LANE_ARRAY`, `VIVID_PORT_STRING_LANES`, old lane views, old lane outputs, lane set IDs, lane execution strategies, and old lane bridge slots.
- Remove or rewrite stale tests that validate old lane behavior.
- Remove old docs or rewrite them as migration notes.
- Add `rg`-based cleanup checks for forbidden active-code terms.
- Keep historical mentions only in archived docs or explicit migration documentation.

Phase 7 is complete when active source no longer depends on old lane-specific surfaces, old operators fail at descriptor/probe time with actionable ABI/schema messages, and runtime has exactly one multiplicity model.

### Phase 8: Hardening And Performance — ✅ COMPLETE

Treat the new model as production runtime infrastructure. (Done on branch `lane-value-phase8`, sub-phases 8a–8f.)

- ✅ Performance benchmarks for scalar / many-valued-frame / audio-lifted / bridge-heavy graphs — `tests/benchmarks/bench_value_graphs.cpp` (8d), with a committed `value_graphs_baseline.json` + an opt-in `tools/bench_regression.py` gate (scalar regression > 15% AND > 0.3 µs).
- ⚠️ Baseline comparison: the pre-removal lane-era baseline was no longer capturable (legacy merged at `6024bc68`), so 8d establishes a **forward** reference on the dev machine instead — honest acceptance, documented in `cmake/CLAUDE.md`.
- ✅ Stress tests — `tests/lanes/test_value_stress.cpp` (8e): identity churn (5000 cycles, bounded + freed), graph recompilation (50× rebuild, provenance well-formed), sustained bridge overflow (100 blocks, counter monotonic). Hot-reload-recompute is covered by `test_hot_reload_classify` (the multiplicity-behavior change → RecompileRequired trigger) + `test_value_flow_runtime` / recompile stress (recompute on rebuild).
- ✅ Audio allocation audit — `tests/audio/test_audio_rt_safety.cpp` (8b): a program-global `operator new` counter proves ZERO heap allocations in the audio callback across LoopBased / InstancePerLane / bridge-overflow paths.
- ✅ Value-flow runtime correctness — `tests/lanes/test_value_flow_runtime.cpp` (8a, all six multiplicity behaviors) + positional-alignment normalization `tests/lanes/test_value_normalization.cpp` (8c).
- Value-flow / identity-lineage debug visualization: covered by the existing `inspect_graph` MCP surface (per-edge multiplicity + `provenance_group_id` + value_count) + UI wire/port provenance coloring (added 7e.5a); no additional overlay was needed.

Phase 8 is complete: scalar-path performance is captured + gated going forward (no meaningful regression without explicit baseline refresh), the audio path is verified real-time safe (zero-alloc), and multiplicity diagnostics (provenance groups, value envelopes) are surfaced through inspect_graph + UI coloring.

## Test Plan

Add compiler tests for:

- scalar-to-scalar flow
- scalar-to-many generation
- many-to-many map behavior
- many-to-one reduce behavior
- collect behavior
- preserve behavior
- incompatible multiplicity diagnostics
- identity loss diagnostics
- audio channel count independent from value multiplicity

Add operator API tests for:

- descriptors
- probing
- codegen
- scaffolding
- ABI rejection for old operators
- typed helpers for float, audio, texture, string, and custom values

Add runtime tests for:

- frame execution
- audio execution
- GPU promotion
- string many-values
- custom payload many-values
- bridge transport
- bridge overflow
- graph recompilation
- hot reload

Add migration and regression tests for:

- representative demo graphs migrated from the old lane system
- seed operators in each domain
- control-server graph mutation
- MCP graph mutation
- UI/inspector representation of multiplicity

Add real-time safety checks for the audio callback:

- no allocation
- no locks
- no descriptor lookup
- no unbounded growth

Add cleanup checks using `rg` for removed public surfaces and old lane-specific terms in active source.

## Acceptance Criteria

- Multiplicity is represented as metadata on first-class runtime values, not as lane-specific port types or side-channel lane buffers.
- The runtime has one value transport model for scalar and many-valued payloads.
- Old lane public surfaces are removed from active operator API and runtime code.
- Old package operators and old graph lane surfaces fail loudly with actionable ABI or schema diagnostics.
- All checked-in seed operators build against the new API.
- All checked-in demo graphs are migrated.
- UI, inspector, control server, MCP, docs, and scaffolding use the new vocabulary.
- Audio channel layout is independent from value multiplicity.
- Audio execution remains real-time safe.
- Bridge overflow and capacity limits are explicit, bounded, and observable.
- Tests cover compiler inference, operator API contracts, frame execution, audio execution, bridge behavior, graph migration, and legacy cleanup.

## Assumptions

- A clean break is acceptable: old packages and operators must be updated rather than supported through runtime compatibility.
- Checked-in graphs and seed operators will be migrated in the same project, not lazily over time.
- A one-time source/schema migration helper is acceptable, but runtime compatibility shims are not.
- User-visible graph behavior should remain equivalent where the old behavior was well-defined.
- macOS-first remains the target; cross-platform concerns should not block the redesign unless they affect public API shape.
- The implementation should prefer a smaller, explicit new contract over preserving old naming or concepts for familiarity.
