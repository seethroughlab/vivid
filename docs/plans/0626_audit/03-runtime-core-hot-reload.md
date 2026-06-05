# Audit 03: Runtime Core & Hot Reload

**Date:** 2026-06-26
**Status:** Audited 2026-06-04 (verify-gated; 10 candidates → 2 confirmed, 8 dismissed)

## Purpose

Audit the application lifecycle, `RuntimeCore` orchestration, hot reload, crash recovery, settings, undo, and file-watching paths for state ownership and reliability risks.

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
- [ ] Record findings with severity, category, evidence, and recommendation.
- [ ] Propose immediate, near-term, and backlog follow-up work.

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
