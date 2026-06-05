# Audit 03: Runtime Core & Hot Reload

**Date:** 2026-06-26
**Status:** Re-audited (maintainability) 2026-06-05 (verify-gated; 9 candidates → 5 confirmed, 4 dismissed). Prior correctness pass retained below; Round-2 maintainability section at end.

## Purpose

Audit the application lifecycle, `RuntimeCore` orchestration, hot reload, crash recovery, settings, undo, and file-watching paths for state ownership and reliability risks.

## Re-Audit Mandate

The prior pass should be treated as a correctness/robustness audit, not a complete code-quality audit.
Run this audit again with equal weight on maintainability: structure, duplication, ownership boundaries,
API clarity, dependency direction, and ease of future change.

Do not mark the audit complete until every checklist item is annotated as `[x]` done, `[~]` partially
covered, or `[ ]` intentionally deferred with a short note. Findings must include both confirmed defects
and structural risks that make future defects likely.

## Scope

- `src/runtime/core/`
- `docs/runtime/architecture.md`
- `docs/runtime/runtime_core.md`
- Runtime core tests under `tests/core/`
- Integration tests that exercise reload, settings, undo, file watching, crash recovery, or startup/shutdown behavior

## Primary Questions

- [ ] Are startup, shutdown, and runtime reinitialization paths explicit and testable?
- [ ] Does `RuntimeCore` own the right state, or is responsibility spread across main-loop helpers?
- [ ] Can hot reload leave stale graph, operator, UI, or audio state behind?
- [ ] Are crash recovery and safe mode reliable after partial initialization failures?
- [ ] Are settings, workspace, source index, and file watcher interactions well bounded?
- [ ] Are undo and command routing consistent with graph mutation semantics?
- [ ] Are large core files still hiding separable lifecycle concerns?

## Subsystem Checklist

- [ ] Trace app bootstrap from process entry to ready runtime.
- [ ] Review `RuntimeCore` interactions with graph compilation, operator registry, UI, audio, and control surfaces.
- [ ] Inspect hot reload and file watcher behavior for duplicate events, missed rebuilds, and stale pointers.
- [ ] Check crash guard, recovery, quarantine, and safe-mode behavior around failed operators.
- [ ] Review settings/workspace/source-index persistence and reload semantics.
- [ ] Verify undo tests cover grouped mutations, reload boundaries, and failed commands.
- [ ] Identify lifecycle behavior that is only covered through manual UI usage.

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

- [x] Map runtime lifecycle responsibilities and identify files/classes/functions that own too many of them. → `main()`'s `tick_frame` lambda is 953 lines (03-R2-F6); otherwise `main.cpp` is reasonably split into `main_helpers`/`main_async_graph`/`main_menu_actions`/`main_package_browser` TUs.
- [x] Look for duplicated reload, build, crash recovery, settings, undo, file-watch, and UI-bridge logic. → post-build init duplicated across `rebuild_live_runtime_from_graph` vs `adopt_prepared_graph` (03-R2-F2, the headline).
- [x] Check whether `RuntimeCore` has a clear boundary from main-loop/UI helper code. → **clean** — the RuntimeCore/caller split (caller owns audio lifecycle, RuntimeCore owns graph/bridge) is intentional (03-R2-F3 refuted).
- [x] Check whether lifecycle APIs expose implementation detail or make invalid states easy to create. → `PreparedBuild::compiled_graph` postcondition is implicit/unguarded (03-R2-F4); `adopt_prepared_build` audio-shutdown contract is header-undocumented (03-R2-F1).
- [x] Identify code that is correct today but fragile under likely reload, async-load, package, or audio-device changes. → the duplicated GPU-sizing + audio-lifecycle sequence (03-R2-F2); device-switch race was refuted (single-threaded tick, 03-R2-F8).
- [x] Produce refactor candidates with priority and expected payoff, separate from bug fixes. → see Round-2 Refactor Candidates below.

## Findings

This subsystem audited **clean on correctness**: the verify pass refuted 8 of 10 candidates (false
premises, mis-cited locations, or already-handled/already-tested behavior). The 2 confirmed findings are
both **Low** — one docs, one test-gap. No state-ownership defects: `RuntimeCore` owns the right state
(`CompiledGraph`, `AudioFrameBridge`, `FrameExecutor`, solo, metronome); the `main_*` helpers hold UI
logic, not core execution state. No lifecycle / hot-reload / crash-recovery / undo correctness findings.

| ID | Severity | Category | Finding | Location |
|----|----------|----------|---------|----------|
| 03-F6 | Low | Docs | `RuntimeCore::prepare_build()` / `adopt_prepared_build()` concurrency contract is undocumented | `src/runtime/core/runtime_core.h:38-52`; `docs/runtime/runtime_core.md:32-53` |
| 03-F10 | Low | Test gap | No test for the live sample-rate-change → recompile path | `src/runtime/core/main.cpp:3212-3223` |

> Both were filed higher (03-F10 Medium) and **downgraded to Low** by the verify pass — 03-F10's failure
> case is already handled gracefully (recompile failure is detected, logged, degrades to "audio offline
> until you pick another device" rather than crashing).

### Evidence & Recommendation

**03-F6 — `prepare_build` / `adopt_prepared_build` concurrency contract undocumented** (Low, Docs · lifecycle)
- *Evidence:* `prepare_build()` (`runtime_core.h:38-52`, marked `const`) compiles a candidate graph off
  the main thread; `adopt_prepared_build()` commits it. Used by `AsyncGraphLoadCoordinator` /
  `AsyncAddCoordinator` (`main_internal.h:94,128` → `main_async_graph.cpp:146,240,408`). `prepare_build()`
  reads shared members (`subgraph_modules_`, `operators_src_dir_`, `safe_mode_`, audio params) without
  locking. The docs (`runtime_core.md:32-53`) describe the *seam* and safe usage ("off the main
  interaction path") but not the **edge-case contract**: may multiple prepares be pending before an
  adopt? may `prepare_build()` run concurrently with `tick()`?
- *Impact:* A future caller (e.g. live package reload) could misuse concurrent-prepare. Mitigated by the
  existing prose; no current defect.
- *Recommendation:* Document in `runtime_core.md`: `prepare_build()` is `const` but **not** thread-safe
  w.r.t. `adopt_prepared_build()`; only one prepared result may be pending; do not call `prepare_build()`
  while `tick()` is executing.

**03-F10 — No test for live sample-rate-change recompile** (Low, Test gap · hot-reload/lifecycle)
- *Evidence:* `main.cpp:3212-3223` polls `audio_engine.consume_pending_session_sample_rate()`; if
  non-zero it calls `runtime.set_audio_sample_rate()` then `rebuild_live_runtime_from_graph()`, logging an
  "audio offline" message on failure. The trigger is set in `audio_engine.cpp:163-170` when a switched
  device's actual rate differs from the session rate. Grep of `tests/` for
  `consume_pending_session_sample_rate` / `rebuild_live_runtime_from_graph` / `set_audio_sample_rate`
  finds nothing — only fixed-rate DSP fixtures.
- *Impact:* The device-switch → recompile glue is untested. Low: the failure path already degrades
  gracefully (no crash); it's hard-to-test live glue (needs the GLFW frame loop + a real rate mismatch).
- *Recommendation:* Unit-test the trivially-testable pieces (`set_audio_sample_rate`, the pending-rate
  consume); a full device-switch integration test is lower-value.

### Test Gaps

Reported separately from findings. Most are realistic reload/failure scenarios worth covering as the
subsystem evolves; none indicates a known defect:

- Live sample-rate change → recompile → audio continuity (03-F10).
- Async `prepare_build`/`adopt_prepared_build` worker-thread integration and the concurrent-`begin`
  rejection guard (`main_async_graph.cpp:94-97,181-184`) — the *unit* of the coordinator's threaded path
  (the prepare/adopt mechanics themselves are already covered by `tests/control/test_runtime_core.cpp`).
- FileWatcher debounce (`file_watcher.cpp` `kDebounceMs=100`): duplicate-event suppression / burst
  coalescing. *(Candidate 03-F4 — its verifier errored, so this is unverified rather than confirmed; the
  100ms debounce mitigates macOS efsw/kqueue duplicate-on-save events.)*
- Hot-reload partial failure (runtime swap succeeds, audio reload fails) state recovery.
- Undo/redo across a hot-reload boundary or after a failed build.
- Multi-lane audio lane-state preservation across hot reload.
- Startup phase-ordering as an explicit assertion (currently implicit but instrumented with `PhaseTimer`).

### Docs to Update (optional polish — several relate to *dismissed* findings)

- `docs/runtime/runtime_core.md` — add the `prepare_build`/`adopt_prepared_build` concurrency contract
  (03-F6).
- `docs/runtime/hot_reload.md` — add a bullet that `lane_behavior`/`strategy_independent` changes redirect
  to a full recompile (the *code* already documents this at every site: `main_helpers.cpp:412-416`,
  `operator_loader.cpp:81-88`, `operator_loader.h:14-20`, `src/runtime/graph/CLAUDE.md:51` — so this is
  doc-completeness only; the basis of dismissed 03-F2).
- `src/runtime/core/hot_reload.h` — note on `reload_operator` that recovery after a failed swap relies on
  the registry's atomic-swap contract keeping the old loader findable (the basis of dismissed 03-F1; the
  guarantee is real, just worth stating).

## Follow-up

**Immediate** — none.

**Near-term** — none. (Subsystem is correctness-clean; nothing rises above Low.)

**Backlog**
- 03-F6: document the prepare/adopt concurrency contract.
- 03-F10: add the small sample-rate-consume unit test.
- Pick from the Test Gaps list as reload/audio-device features change.
- Apply the optional doc clarifications above.

### Dismissed (verification-refuted)

Eight candidates were refuted (one had a verifier error and is therefore *unverified*, not a confirmed
refutation — see FileWatcher in Test Gaps):

- **03-F1** (hot-reload recovery "fragile") — false trigger condition: the old-loader fallback runs
  **only when the dylib swap fails**; `OperatorLoader::load()` is an atomic swap (dlopen-new-first, no
  state mutation on failure), so the old loader is always still findable. The claimed crash precondition
  can't arise.
- **03-F2** (lane/strategy redirection "undocumented") — already documented verbatim at the code sites
  (`main_helpers.cpp:412-416` et al.); only `hot_reload.md` lacks a bullet. Cited locations were wrong.
- **03-F3** (async prepare/adopt "orphaned state") — `take_completed()` unconditionally drains the result
  and both success/failure branches are handled by RAII; no cancellation path or leak exists. Partial
  coverage already in `tests/control/test_runtime_core.cpp` + `tests/ui/test_ui_overlay_interactions.cpp`.
- **03-F4** (FileWatcher debounce untested) — **verifier errored; unverified.** Folded into Test Gaps as
  a plausible low test-gap, not a confirmed finding.
- **03-F5** (undo lacks reload/build metadata) — correct-by-design: undo is JSON-only and re-flows through
  the normal compile/load path (where safe-mode enforcement lives); not a defect.
- **03-F7** (HotReloader swallows errors when `build_console` null) — false: the error is captured into
  `ReloadResult.error_output` and enqueued **unconditionally** of `build_console`; the null branch only
  adds supplementary UI output.
- **03-F8** (safe-mode/quarantine recovery untested) — extensively covered already:
  `tests/graph/test_graph_compiler_safe_mode.cpp`, `tests/core/test_quarantine.cpp`,
  `tests/graph/test_graph_snapshot_builder.cpp`, `tests/control/test_control_server_crash.cpp`. "Unquarantine"
  isn't a stateful op (relaunch without `--safe-mode`).
- **03-F9** (startup GPU/scan ordering "implicit") — false premises: shader scanning never touches the GPU
  (`operator_registry_scan.cpp:573` only reads files), project-shader scanning is structurally inside the
  graph-load path, and `main()` already has labeled phase banners + `PhaseTimer` instrumentation.

## Completion Criteria

- [x] Findings table is filled in or explicitly marked with no findings.
- [x] Lifecycle, hot reload, crash recovery, and undo findings are distinguished. *(No correctness
  findings in any category; the 2 Low findings are tagged lifecycle/hot-reload.)*
- [x] Each state ownership concern identifies the current owner and preferred owner. *(No ownership
  defects — `RuntimeCore` owns core execution state; `main_*` helpers own UI logic. Current = preferred.)*
- [x] Test gaps include realistic reload and failure scenarios.
- [x] Follow-up work is grouped into immediate, near-term, and backlog.

---

# Maintainability Re-Audit (Round 2) — 2026-06-05

Verify-gated maintainability pass per the Re-Audit Mandate (round 1 was correctness-focused). **9 candidates
→ 5 confirmed (1 Medium, 4 Low), 4 dismissed.** *(The first finder run under-delivered — 1 finding, never
read `main.cpp`; re-run with an explicit "read the big files" directive produced the real sweep.)* The
`RuntimeCore` ↔ caller boundary is **clean** (the split is intentional — the caller owns audio lifecycle,
RuntimeCore owns graph/bridge), and `main.cpp` is reasonably partitioned across `main_*` TUs. The verify
pass refuted 4 candidates: a "hardcoded 1280×720" that is the `kDefaultTexW/H` constants used everywhere
(03-R2-F5), a device-switch "race" in single-threaded synchronous tick code (03-R2-F8, the finder even
cited a non-existent line), and two "undocumented invariant" claims that are already documented in code
(03-R2-F3/F7).

| ID | Severity | Category | Finding | Location |
|----|----------|----------|---------|----------|
| 03-R2-F2 | Medium | Maintainability | Post-build init is **duplicated** between `rebuild_live_runtime_from_graph` and `adopt_prepared_graph` — a ~16-line identical tail (GPU texture alloc w/ `kDefaultTexW/H`, audio engine shutdown→build→start, capture-coordinator wiring) plus a duplicated prologue; only the `build()` vs `adopt_prepared_build()` call differs | `src/runtime/core/main_async_graph.cpp:360-394` & `396-429` |
| 03-R2-F6 | Low | Maintainability | `main()`'s `tick_frame` lambda is **953 lines** (2579–3532) — the entire per-frame body (window/display state, async-transaction polling, capture/analysis, surface present, UI render) inlined as one lambda | `src/runtime/core/main.cpp:2579-3532` |
| 03-R2-F4 | Low | Maintainability | `PreparedBuild::compiled_graph` postcondition (must be non-null) is implicit + undocumented; `adopt_prepared_build` dereferences it unguarded (`*compiled_graph_`) whereas `tick()` null-checks | `runtime_core.h:38-42`; `runtime_core.cpp:105-137` |
| 03-R2-F1 | Low | Docs | `adopt_prepared_build()` header omits the audio-engine-shutdown contract (it's in `runtime_core.md` but not the header doxygen); callers do it correctly but the API doesn't state it | `src/runtime/core/runtime_core.h:58` |
| 03-R2-F9 | Low | Test gap | No tests for async-adoption lifecycle error/edge cases (null `PreparedBuild`, adoption-order, concurrent `prepare_build`, failed `audio_engine.build()` aftermath) — only the happy path (Test 1b) | `tests/control/test_runtime_core.cpp` |

> All findings are **non-lane** (runtime lifecycle) → fixable normally (not deferred into the lane-value
> clean-break). 03-R2-F4 filed Medium → **Low** (callers are correct; the gap is a missing guard/doc, not a
> live defect).

### Evidence & Recommendation

**03-R2-F2 — duplicated post-build init** (Medium, Maintainability — *the headline*)
- *Evidence:* `rebuild_live_runtime_from_graph` (360-394) and `adopt_prepared_graph` (396-429) share a
  byte-identical (modulo brace style) post-build tail — `graph_loaded` from `compiled_graph()` (377 vs 413),
  `has_gpu_ops` + `allocate_gpu_textures(device, kDefaultTexW, kDefaultTexH, RGBA16Float)` +
  `find_effective_gpu_sink` (378-384 vs 414-420), audio `shutdown→build→start` gated on `has_audio_operators`
  (386-390 vs 422-425), `capture_coordinator.set_audio_engine` (392 vs 427) — and a duplicated prologue
  (audio shutdown / `runtime.shutdown()` / `thumb_cache.clear()`). The only real divergence is
  `runtime.build(graph, registry)` (368) vs `runtime.adopt_prepared_build(std::move(prepared))` (408).
- *Impact:* the dangerous GPU-sizing + audio-lifecycle sequence must be kept in sync across two functions; a
  future change to one (and not the other) is a latent state/lifecycle bug.
- *Recommendation (refactor candidate):* extract `post_build_init(MainAppContext&, …)` taking the
  already-built/adopted state; the rebuild path keeps its build-failure cleanup (369-374) and the adopt path
  its `ctx.graph = std::move(next_graph)` + optional metronome reset (407-411). Behavior-neutral.

**03-R2-F6 — `tick_frame` 953-line lambda** (Low, Maintainability)
- *Evidence:* `auto tick_frame = [&]() -> bool {` at 2579 closes at 3532 — one ~953-line lambda owning the
  full frame body; the next lambda (`poll_events`) starts at 3535. The phases are already comment-delimited
  inside it (test injection, hot-reload poll, pre/post audio sync, tick, UI render, surface present).
- *Impact:* the per-frame orchestration is hard to navigate/modify; phase ordering invariants are inline.
- *Recommendation:* extract the comment-delimited phases into named `tick_*` helper lambdas/functions
  (member or file-local). **Priority low** (it works and is sectioned) — lower than 03-R2-F2.

**03-R2-F4 / 03-R2-F1 — lifecycle API contracts** (Low)
- *Recommendation:* add an early guard + doxygen postcondition to `adopt_prepared_build` (`PreparedBuild`
  must carry a non-null `compiled_graph`) and a one-line audio-shutdown-contract note on the header. Tiny,
  safe.

### Refactor Candidates (priority + payoff — separate from bug fixes)
1. **Extract `post_build_init()`** (03-R2-F2) — **priority medium, payoff high.** Removes the
   keep-in-sync hazard on the GPU/audio lifecycle sequence; behavior-neutral; small.
2. **Decompose `tick_frame`** (03-R2-F6) — priority low/medium, payoff medium. Behavior-neutral but touches
   the main loop; sequence after #1.
3. **`adopt_prepared_build` guard + doc** (03-R2-F4 + 03-R2-F1) — priority low, payoff low (safety/clarity).

### Test Gaps (refactor-safety)
- Async-adoption error/edge cases (03-R2-F9): null `PreparedBuild`, adoption order, concurrent
  `prepare_build`, failed `audio_engine.build()` recovery — would guard the 03-R2-F2 extraction.

### Dismissed (verification-refuted)
- **03-R2-F3** (adoption logic "split" between RuntimeCore and callers) — refuted: the split is intentional
  and documented (RuntimeCore doesn't own AudioEngine; caller owns audio lifecycle). Not a smear.
- **03-R2-F5** (hardcoded 1280×720 GPU texture size) — refuted: 1280×720 are `kDefaultTexW/kDefaultTexH`
  (`main.cpp:122-123`) used at every allocate site; not a magic number.
- **03-R2-F7** (post_tick audio-sync order not self-evident) — refuted: already documented at
  `main.cpp:3231-3236`.
- **03-R2-F8** (device-switch audio rebuild not guarded by `graph_transaction_active`) — refuted: the
  per-frame tick is single-threaded synchronous; no race. (Finder cited a non-existent line 3879.)

## Round-2 Follow-up
- **DONE 2026-06-05 (03-R2-F2/F4/F1/F9):** extracted `teardown_live_runtime()` + `finalize_live_runtime()`
  in `main_async_graph.cpp` (both functions now share them; behavior-neutral); added a null-`compiled_graph`
  guard + postcondition doc to `RuntimeCore::adopt_prepared_build` and an audio-shutdown-contract note on its
  header; added `test_runtime_core` Test 1c (null-`PreparedBuild` adopt is a guarded no-op). Build + tests
  green.
- **Deferred — backlog (03-R2-F6):** decompose the 953-line `tick_frame` lambda into phase helpers — a large
  main-loop refactor, Low priority, already comment-sectioned; its own focused effort (orthogonal to the
  lane-value clean-break, so it survives). Also backlog: the harder async-adoption error tests from 03-R2-F9
  (concurrent `prepare_build`, device-switch race — need threading/device harness).
