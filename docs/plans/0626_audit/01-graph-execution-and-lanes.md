# Audit 01: Graph Execution & Lanes

**Date:** 2026-06-26
**Status:** Re-audited (maintainability) 2026-06-05 (verify-gated; 8 candidates → 5 confirmed, 3 dismissed). Prior correctness pass retained below; Round-2 maintainability section at end.

## Purpose

Audit the graph compiler and execution model for correctness risks in topology compilation, lane propagation, executor behavior, and cross-cadence state transfer.

## Re-Audit Mandate

The prior pass should be treated as a correctness/robustness audit, not a complete code-quality audit.
Run this audit again with equal weight on maintainability: structure, duplication, ownership boundaries,
API clarity, dependency direction, and ease of future change.

Do not mark the audit complete until every checklist item is annotated as `[x]` done, `[~]` partially
covered, or `[ ]` intentionally deferred with a short note. Findings must include both confirmed defects
and structural risks that make future defects likely.

## Scope

- `src/runtime/graph/`
- `docs/runtime/graph.md`
- `src/runtime/graph/CLAUDE.md`
- Graph, lane, executor, and integration tests under `tests/graph/`, `tests/lanes/`, `tests/ops/`, and `tests/integration/`
- Demo graphs that exercise lane-heavy or mixed-cadence behavior

## Primary Questions

- [ ] Are graph compilation passes documented, ordered, and enforced consistently?
- [ ] Are lane identity, lane provenance, and lane cardinality preserved across operators and recompiles?
- [ ] Are frame-cadence and audio-cadence execution boundaries explicit and race-safe?
- [ ] Are graph snapshots complete enough for UI, control server, and MCP consumers?
- [ ] Are invalid graph states rejected early with useful diagnostics?
- [ ] Are topology changes, hot reload, and recompilation handled without stale execution state?
- [ ] Do tests cover scalar, multi-lane, GPU-backed lane, and audio-lane cases?

## Subsystem Checklist

- [ ] Trace graph JSON load through `GraphCompiler` to `CompiledGraph`.
- [ ] Review compile-pass invariants and confirm each pass has narrow ownership.
- [ ] Inspect `FrameExecutor` and `AudioExecutor` for shared assumptions.
- [ ] Check lane buffer, lane state, and lane output adapter ownership/lifetime.
- [ ] Verify graph snapshot fields against UI/control/MCP expectations.
- [ ] Review test fixtures for lane alignment, disconnected ports, invalid connections, and graph reloads.
- [ ] Identify oversized or multi-purpose graph files that obscure compiler or executor contracts.

## Audit Checklist

- [ ] Read the relevant subsystem docs and navigation guides.
- [ ] Inspect the main source files and ownership boundaries.
- [ ] Review tests that claim to cover the subsystem.
- [ ] Check docs/code/test contract drift.
- [ ] Identify correctness, robustness, and maintainability findings.
- [ ] Identify oversized files, mixed responsibilities, fragile seams, and unclear ownership.
- [ ] Identify duplicated logic or repeated patterns that should be shared or intentionally documented.
- [ ] Check dependency direction and public/private API boundaries.
- [ ] Check whether tests make future refactors safe, not just whether they cover the latest fix.
- [ ] Record findings with severity, category, evidence, and recommendation.
- [ ] Propose immediate, near-term, and backlog follow-up work.

## Required Maintainability Review

- [x] Map the main responsibilities in this subsystem and identify files/classes/functions that own too many of them. → `tick()` and `audio_callback()` monoliths (01-R2-F1/F2).
- [x] Look for duplicated validation, routing, snapshot, lane, executor, and diagnostic logic. → missing-operator error overlap (01-R2-F3), context-field init dup (01-R2-F4).
- [x] Check whether compiler/executor APIs expose implementation detail or force awkward callers. → no confirmed issue (F5 Options-struct and F8 build()-coupling both refuted; F6 lane_set_id already has `is_scalar()`).
- [x] Check whether dependency direction matches the architecture docs. → correct (runtime→UI types one-way; no upward deps).
- [x] Identify code that is correct today but fragile under likely future graph, lane, hot-reload, or cadence changes. → bridge >1024-lane truncation has no compile-time guard (01-R2-F7).
- [x] Produce refactor candidates with priority and expected payoff, separate from bug fixes. → see Round-2 Refactor Candidates below.

## Findings

| ID | Severity | Category | Finding | Location |
|----|----------|----------|---------|----------|
| 01-F2 | Medium | Correctness | Lane metadata not re-propagated during operator hot reload | `src/runtime/graph/graph_compiler_reload.cpp:9-127` |
| 01-F9 | Low | Robustness | Lane-pool resize failure yields a silent empty lane buffer (no diagnostic) | `src/runtime/graph/frame_executor.cpp:141-150` (also 158, 188, 197, 235) |
| 01-F6 | Low | Test gap | No tests for GPU-backed lane promotion | `src/runtime/graph/graph_compiler_planning.cpp:210` (no test in `tests/lanes/`, `tests/gpu/`) |
| 01-F7 | Low | Test gap | No tests for lane execution-strategy changes during operator hot reload | `src/runtime/graph/graph_compiler_reload.cpp` + `tests/lanes/` |

> Severities reflect the adversarial verify pass: 01-F2 was filed as High and downgraded to Medium
> (dev-time-only trigger, bounds-checked, no crash); 01-F6 and 01-F7 were filed as Medium and
> downgraded to Low (narrow, guarded paths with no demonstrated defect).

### Evidence & Recommendation

**01-F2 — Lane metadata not re-propagated during operator hot reload** (Medium, Correctness)
- *Evidence:* `GraphCompiler::reload_operator()` (`graph_compiler_reload.cpp:9-127`) destroys/recreates
  operator instances and re-runs `init_frame_state` / `init_audio_state` to restore ports and params,
  but never re-runs Pass 2.6 lane-set propagation (`graph_compiler.cpp:540-668`) and never re-assigns
  `cn.lane_behavior` (set only at `graph_compiler.cpp:107` during full compile). The hot-reload driver
  (`main_helpers.cpp:395`) calls `reload_operator()` in place for existing types with **no** follow-up
  `request_recompile()` (that only fires for newly-scaffolded ops at line 376). The frame executor
  feeds `cn.input_lane_sets[p].lane_set_id` as provenance (`frame_executor.cpp:335-338`), so stale
  metadata yields wrong provenance — bounds-checked, no crash.
- *Impact:* If an operator author edits the compile-time `kLaneBehavior` (Pointwise→Structural, etc.)
  or changes port shape and **hot-reloads instead of restarting**, downstream lane-aware (Pointwise)
  nodes can receive the wrong `lane_set_id`, silently violating the shared-provenance invariant.
  Conditional, developer-only; the common hot-reload case (unchanged ports/behavior) is unaffected.
- *Recommendation:* After `reload_operator()`, either re-run a focused lane-propagation pass for the
  reloaded nodes, or mark the compiled graph dirty to force a full recompile on the next cycle.
  Relatedly (see 01-F7), `hot_reload_descriptor_compatible()` (`operator_loader.cpp:64-72`) does not
  reject changes to `strategy_independent` (`types.h:188`), so a stale `frame_execution_strategy` can
  also survive a reload — extend the compatibility check to cover it.

**01-F9 — Lane-pool resize failure yields a silent empty lane buffer** (Low, Robustness)
- *Evidence:* `frame_executor.cpp:141-150` does `buf->resize(len); if (dst) { …fill+commit… }` and
  silently leaves the buffer uncommitted (`committed_length==0`) when `resize()` returns `nullptr`;
  same un-diagnosed pattern at lines 158, 188, 197, 235. **Correction to the original filing:** the
  real trigger is *not* `max_lane_elements` (16M) — that field is declared (`compiled_graph.h:534`)
  but never used in any code path. `resize()` returns `nullptr` only for `pool_owned` buffers when
  `len` exceeds the pool's pre-allocated capacity (default 1024), i.e. **>1024 lanes through a
  remapped / lane-count-normalized wire** (zero-copy passthrough and non-pool operator-output buffers
  are unaffected).
- *Impact:* Large remapped lane arrays silently produce empty buffers downstream with no node-error
  or warning; consumers see truncated data. Narrow (remap/expand wires >1024 lanes only).
- *Recommendation:* On `resize()` → `nullptr`, mark the node errored ("lane buffer exceeded pool
  capacity") and zero the output ref; consider growing the pool buffer capacity for wide-lane graphs.

**01-F6 — No tests for GPU-backed lane promotion** (Low, Test gap)
- *Evidence:* `plan_gpu_lane_promotion()` (`graph_compiler_planning.cpp:210`, called from
  `graph_compiler.cpp:855`) promotes lane arrays ≥ `kGpuLanePromotionThreshold` (256,
  `graph_compiler_internal.h:14`) to GPU storage-buffer backing; the frame executor lazily uploads at
  `frame_executor.cpp:384-401`. Grep across `tests/` for `plan_gpu_lane_promotion`,
  `lane_input_gpu_promoted`, `kGpuLanePromotionThreshold` returns zero hits.
- *Impact:* Guarded, narrow path (>256-element lanes feeding GPU-only consumers) is entirely untested;
  no demonstrated defect.
- *Recommendation:* Add `test_gpu_lane_promotion.cpp`: small lanes not promoted; large lanes promoted;
  upload happens on tick; multiple GPU consumers share the upstream buffer; cleanup on dealloc.

**01-F7 — No tests for lane strategy changes during operator hot reload** (Low, Test gap)
- *Evidence:* `reload_operator()` leaves `cn.frame_execution_strategy` (set at `graph_compiler.cpp:850`)
  stale; `hot_reload_descriptor_compatible()` (`operator_loader.cpp:64-72`) does not check
  `strategy_independent`, so a `v1(0)→v2(1)` reload is accepted while strategy stays `Scalar`.
  `test_hot_reload*.cpp` and `test_frame_lane_lifting.cpp` exist but none combine reload with a
  strategy change.
- *Impact:* Same dev-time-only window as 01-F2; full recompile recomputes strategy correctly.
- *Recommendation:* Add `test_operator_reload_lane_strategy_change.cpp` and tie the fix to 01-F2's
  compatibility-check extension.

### Test Gaps

Reported separately from implementation findings (01-F6 and 01-F7 above are the verified subset):

- GPU-backed lane promotion (`kGpuLanePromotionThreshold`) — promotion, GPU upload, multi-consumer sharing.
- Lane execution-strategy change during hot reload (Scalar→LoopBased).
- Audio-lane `InstancePerLane` with multi-channel inputs across hot reload.
- Lane cardinality mismatch when a `LoopBased` frame node feeds an `InstancePerLane` audio node.
- Lane identity preservation across full graph recompile (lane IDs re-allocated from 1).
- Lane-state retirement under high-polyphony concurrent notes (mutex-contention stress).
- Broken connections with disconnected lane ports in missing-operator placeholders.
- Snapshot refresh after a topology change involving different-`lane_behavior` operators.
- Snapshot edge metadata (`lane_set_id`, `lane_count`, `data_type`) correctness across all edge types.
- Frame lane lifting (`LoopBased`) with identity-bearing lane IDs from an upstream Reduction operator.

### Docs to Update

- `docs/runtime/graph.md` — expand "Lane Transport (CompiledGraph)" to document `identity_bearing`
  semantics, `lane_id` allocation/retirement lifecycle, per-lane state on retirement, and the explicit
  note that lane identity is **not** preserved across a full recompile (IDs re-allocated from 1).
- `src/runtime/graph/CLAUDE.md` — document the Pass numbering (1, 2, 2.6, 3, 4a–4e, 5, 6, 7) and add:
  "Hot reload (`reload_operator`) does not re-run Pass 2.6, so lane metadata may be stale if operator
  `lane_behavior`/`strategy_independent` changes." (Captures 01-F2/01-F7.)
- `docs/runtime/graph_snapshot_contract.md` — note that `LaneExecutionStrategy`
  (Scalar/InstancePerLane/LoopBased) is intentionally **not** in `GraphSnapshot` (compile-time state).
  (Note: `lane_behavior` *is* already in the snapshot — `graph_snapshot.h:198` — contrary to the
  dismissed 01-F8.)

## Follow-up

**Immediate** — none. No confirmed High-severity / production-path issue.

**Near-term** — ✅ **DONE 2026-06-04** (build + tests green)
- 01-F2 + 01-F7: `classify_hot_reload()` now returns `RecompileRequired` when a reload changes
  `lane_behavior` or `strategy_independent`; the reload driver calls `request_recompile()` so the
  change applies live via a correct full rebuild. (`operator_loader.{h,cpp}`, `main_helpers.cpp`)
- 01-F9: `FrameExecutor::lane_pool_` is now growable (`allow_grow` on `LaneBuffer`, growable
  `LaneBufferPool`); wide remapped/expanded lanes grow instead of truncating, with a one-shot
  diagnostic guarding the (now-unreachable) failure. (`lane_buffer.h`, `lane_buffer_pool.h`,
  `frame_executor.{h,cpp}`)

**Backlog**
- ✅ GPU-lane-promotion test (`tests/lanes/test_gpu_lane_promotion.cpp`) and hot-reload classification
  test (`tests/core/test_hot_reload_classify.cpp`) added (01-F6, 01-F7).
- ✅ Three doc updates applied (`docs/runtime/graph.md`, `src/runtime/graph/CLAUDE.md`,
  `docs/runtime/graph_snapshot_contract.md`).
- Work through the broader test-gap list as lane features harden (remaining items in Test Gaps).

### Dismissed (verification-refuted)

Six candidates were refuted by the verify pass and are **not** findings:

- **01-F1** (pass-numbering inconsistency) — cited `graph_compiler.h:19-26` does not list passes; the
  `.cpp` summary and inline labels already use consistent decimal notation (2.6, 4a–4e).
- **01-F3** (lane-retirement mutex contention on the audio thread) — there are two separate
  `LaneStateService` instances, one per executor/thread; no cross-thread contention is possible, and
  the single-thread lock is uncontended (pre-reserved to 128, lock-free fast path).
- **01-F4** (oversized `AudioNodeState`) — opinion-level; the "allocates all three buffer layouts"
  premise is false (one `buffers_in`/`buffers_out`, sized by a clean exhaustive if/else); no defect.
- **01-F5** (lane-identity not documented) — the cited range already contains a "Lane State Lifecycle"
  section documenting retirement; finding mislabeled the section.
- **01-F8** (snapshot missing `lane_behavior`) — false premise: `lane_behavior` *is* in the snapshot
  (`graph_snapshot.h:198`, populated `graph_snapshot_builder.cpp:83`); `active_cadence` also present.
- **01-F10** (frame lane-context init "never resized") — false: `frame_executor.cpp:41` calls
  `frame_lane_contexts_.resize(cg.frame_order.size())` every tick; no inconsistency window.

## Completion Criteria

- [x] Findings table is filled in or explicitly marked with no findings.
- [x] Each finding has evidence, impact, and a recommended next action.
- [x] Test gaps are listed separately from implementation findings.
- [x] Docs that need updates are identified by path.
- [x] Follow-up work is grouped into immediate, near-term, and backlog.

---

# Maintainability Re-Audit (Round 2) — 2026-06-05

Verify-gated maintainability/code-quality pass per the Re-Audit Mandate (round 1 above was
correctness-focused). **8 candidates → 5 confirmed (2 Medium, 3 Low), 3 dismissed.** The headline is two
**executor monoliths** the correctness pass didn't flag. Dependency direction is correct (runtime→UI
one-way). The verify pass refuted 3 over-reaching candidates (a "conflated" `Options` struct with no real
caller friction; a "missing" `is_scalar()` accessor that **already exists** at `lane_types.h:27`; a
`build()` bridge-coupling that is actually used by `start()`/`process_audio_for_test()`).

| ID | Severity | Category | Finding | Location |
|----|----------|----------|---------|----------|
| 01-R2-F1 | Medium | Maintainability | `FrameExecutor::tick()` is one ~800-line method (lines 38–842) owning per-tick init, 4-wire-type lane-aware propagation w/ COW, GPU dispatch, LoopBased per-lane processing, normal dispatch, and readback | `src/runtime/graph/frame_executor.cpp:38-842` |
| 01-R2-F2 | Medium | Maintainability | `AudioExecutor::audio_callback()` is one ~700-line RT method (448–~1140) inlining lane-lift deinterleave→process→interleave, bypass, LoopBased + normal dispatch, and overrun diagnostics | `src/runtime/graph/audio_executor.cpp:448-~1140` |
| 01-R2-F3 | Low | Maintainability | Snapshot builder re-derives ABI-mismatch/missing-operator diagnostics per-frame (re-queries `registry.abi_mismatch_diagnostics()`) instead of reusing the `CompiledNode` fields the compiler already computed | `graph_compiler.cpp:162-187` + `graph_snapshot_builder.cpp:125-160` |
| 01-R2-F4 | Low | Maintainability | `VividFrameContext`/`VividAudioContext` ~25-field init is duplicated across **5 sites** (frame 626/691; audio 767/881/953); adding a context field needs N edits | `frame_executor.cpp:626-658,691-715`; `audio_executor.cpp:767-797,881-911,953-983` |
| 01-R2-F7 | Low | Robustness | Audio→frame bridge silently **truncates** lane arrays >1024 (`kDefaultLaneCapacity`); no compile-time validation (the per-port-capacity override noted in `graph_compiler_internal.h` is unimplemented) | `snapshot_types.h:21-26`; `audio_frame_bridge.cpp:221`; `graph_compiler*.cpp` (no guard) |

> 01-R2-F7 was filed Medium and **downgraded to Low**: the overflow is *not silent* — it surfaces a
> Warning-severity runtime-health finding (`runtime_health.cpp:265-272`, exported in health JSON) plus a
> rate-limited stderr line. Data is still clamp-truncated (real gap: no compile-time rejection / no slot
> growth), but the ">1024 polyphonic voices on a bridge edge" threshold is extreme.

### Evidence & Recommendation

**01-R2-F1 / 01-R2-F2 — executor monoliths** (Medium, Maintainability)
- *Evidence:* `tick()` is a single method 38→842 (next method `set_solo` at 844); `audio_callback()` a
  single method from 448. Each interleaves the genuinely novel logic (lane propagation / lane-lifting)
  with setup, dispatch, and readback in deeply nested conditionals. Telemetry is *already* extracted in
  the audio path (`write_audio_node_telemetry` etc.), so that part of F2's recommendation is moot.
- *Impact:* a lane-propagation or lane-strategy change must be made inside ~100-line nested blocks of
  unrelated boilerplate (RT-critical in the audio case), raising regression/RT-safety risk; the
  propagation logic has no separable entry point for unit testing.
- *Recommendation (refactor candidate):* extract pure helpers with no behavior change —
  `propagate_frame_direct_edges()`, `init_node_frame_context()` (frame) and `process_lifted_node()`,
  `process_bypassed_node()` (audio). Prove parity with the existing graph/lane tests + a build.

**01-R2-F3 — duplicated missing-operator diagnostic derivation** (Low)
- *Evidence:* the compiler stores `missing_operator_reason`/`_detail` on `CompiledNode` (Pass 1); the
  snapshot builder, rebuilt every frame, re-queries `abi_mismatch_diagnostics()` for the generic
  abi/not-found branch rather than reusing those fields. *(Scope note: the `reason` enum is deliberately
  structured and consumed by many call sites — `control_server_checks/query/dispatch`, `project_lockfile` —
  so do NOT collapse it to a string; just have Pass 1 also stash the verbose UI message for reuse.)*
- *Recommendation:* store the synthesized UI message on `CompiledNode` at compile time; snapshot builder
  copies it. Low priority.

**01-R2-F4 — context-field init duplication** (Low)
- *Evidence:* 5 near-identical ~25-field assignment blocks. The shared metronome subset is *already*
  factored (`populate_metronome_context()` template in `executor_common.h`).
- *Recommendation:* per-executor `populate_frame_context()` / `populate_audio_context()` helpers for the
  common field block (a single cross-executor template is impractical — different ctx struct types).
  Low priority.

### Refactor Candidates (priority + payoff — separate from bug fixes)
1. **Decompose `tick()` / `audio_callback()`** (01-R2-F1/F2) — **priority: medium, payoff: high.** Biggest
   single maintainability win in the subsystem; isolates the hard lane logic, enables unit tests, lowers
   RT-edit risk. Behavior-neutral extraction; guard with existing tests + targeted new ones.
2. **`populate_{frame,audio}_context()` helpers** (01-R2-F4) — priority: low, payoff: medium. Removes the
   "forgot a field" hazard; small, mechanical.
3. **Precompute missing-operator UI message on `CompiledNode`** (01-R2-F3) — priority: low, payoff: low.
4. **Compile-time bridge lane-capacity validation** (01-R2-F7) — priority: low, payoff: medium. A planner
   check that rejects (or a growable `BridgeLaneSlot`) closes the truncation gap; pairs with a test.

### Test Gaps (refactor-safety — would catch a bad executor decomposition)
- Lane-state identity across the audio→frame bridge crossing, and across a full recompile (clear/remap).
- Mixed-cadence lane propagation (audio polyphonic value → bridge → frame analysis consumer).
- Bridge lane overflow (>1024) — rejection / graceful degradation.
- Frame lane-execution strategy stability under a Pass-4 refactor (LoopBased vs Scalar not silently
  downgraded). These lock in the contracts the F1/F2 extraction would touch.

### Docs to Update
- `docs/runtime/graph.md` — lane-identity-not-preserved-across-recompile note; bridge lane-capacity limit.
- `src/runtime/graph/CLAUDE.md` / `docs/ARCHITECTURE.md` — spell out the Pass-4 sub-passes + "one
  responsibility per pass" guidance.

### Dismissed (verification-refuted)
- **01-R2-F5** (`GraphCompiler::Options` conflates concerns) — refuted: GPU/audio fields have default
  member initializers and there is a **single** `compile()` caller, so the claimed "must construct all
  fields / update every call" friction doesn't exist; `disabled_node_ids` is legitimately compiler input.
- **01-R2-F6** (lane_set_id has no accessor) — refuted: `LaneSet::is_scalar()` already exists
  (`lane_types.h:27`) and is the dominant access pattern; executors don't read `edge.lane_set_id` directly.
- **01-R2-F8** (`AudioExecutor::build()` bridge/metronome early-binding) — refuted: `bridge_`/`graph_` are
  used by `start()`, `restart_device()`, and `process_audio_for_test()`; `wall_time` capture at build is
  the intended metronome-anchor. Speculative split guarded on a non-roadmap hypothetical.

## Round-2 Follow-up
- **DONE 2026-06-05 (01-R2-F1/F2/F4):** decomposed both executor monoliths, behavior-neutral, parity-proven
  (full audio + graph/lane suite, 66 tests, incl. `test_demo_graphs`). `FrameExecutor::tick()` ~800→~316
  lines, extracted `propagate_frame_direct_edges` / `process_gpu_node` / `process_loopbased_node` /
  `process_control_node` / `populate_frame_context`. `AudioExecutor::audio_callback()` per-node dispatch
  ~258→9 lines, extracted `process_lifted_audio_node` / `process_loopbased_audio_node` /
  `process_normal_audio_node` / `populate_audio_context` (F4 collapses the triplicated VividAudioContext
  init). Committed on `audit-01-maint-fixes`.
- **Deferred — backlog (with rationale):**
  - **01-R2-F3** (precompute missing-op UI message): narrow Low DRY nit — the per-frame re-derivation only
    fires for *missing/errored* nodes (not a hot path), and the proper fix must keep the structured
    `missing_operator_reason` enum (widely consumed by control_server/project_lockfile). Worth doing, low
    priority.
  - **01-R2-F7** (compile-time bridge lane-capacity guard): robustness, not maintainability; the >1024
    overflow is already surfaced via `runtime_health` (verifier downgraded to Low), and a guard is a
    feature-add (new diagnostic / growable slot), not a behavior-neutral refactor.
  - Refactor-safety test gaps (lane-state across recompile/bridge; strategy stability) + graph.md /
    ARCHITECTURE.md doc updates.
