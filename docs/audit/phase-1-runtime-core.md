# Phase 1 — Runtime Core Stability

## Scope Reviewed

Primary runtime surfaces reviewed in this phase:

- `src/runtime/main.cpp`
- `src/runtime/runtime_api.cpp`
- `src/runtime/scheduler.cpp`
- `src/runtime/audio_engine.cpp`
- `src/runtime/gpu_context.cpp`
- `src/runtime/hot_reload.cpp`

Supporting runtime contracts reviewed:

- `docs/runtime/architecture.md`
- `docs/runtime/audio_engine.md`
- `docs/runtime/gpu.md`
- `docs/runtime/hot_reload.md`

This phase focused on lifecycle guarantees and runtime stability, not UI polish or broader operator/package ergonomics except where they affect runtime safety.

## Evidence Gathered

### Automated runtime bundle

Ran:

```bash
ctest --test-dir build --output-on-failure -R "test_runtime_api|test_scheduler|test_audio_engine|test_hot_reload|test_audio_hot_reload|test_runtime_stress|test_hot_reload_stress|test_mixed_runtime_stability|test_capture_coordinator|test_export_pipeline|test_app_update_manager|test_demo_graphs"
```

Observed result:

- 13 of 13 matched tests passed

Passing lanes in this bundle:

- `test_runtime_api`
- `test_scheduler`
- `test_audio_engine`
- `test_hot_reload`
- `test_audio_hot_reload`
- `test_runtime_stress`
- `test_hot_reload_stress`
- `test_mixed_runtime_stability`
- `test_capture_coordinator`
- `test_export_pipeline`
- `test_app_update_manager`
- `test_demo_graphs`
- `test_hot_reloader_queue`

Note:

- `test_hot_reloader_queue` also matched the current regex bundle and passed

### Focused harness triage

Ran:

```bash
ctest --test-dir build --output-on-failure -R "test_demo_graphs"
./build/test_demo_graphs ./build/graphs
```

Observed result:

- `ctest` path now passes
- direct invocation also passes cleanly:
  - `19 passed`
  - `0 failed`
  - `65 skipped`

Runtime signal added during this follow-up:

- per-graph checkpoint logging around load, build, tick, cleanup, and result
- optional graph filter for narrow repro
- plugin/preset discovery anchored to the executable directory instead of the caller working directory

Current classification:

- the prior `ctest` vs direct-run discrepancy is resolved
- the headless smoke lane is now deterministic in this environment

Important environment note:

- current headless runs report `No GPU — GPU-only graphs will be skipped`
- this means the lane is currently validating control/audio/runtime smoke plus existing deferred classes
- it is not currently providing positive headless execution coverage for GPU-only demo graphs on this machine

### Registry scan / startup-adjacent smoke

Ran:

```bash
./build/vivid list-packages
```

Observed result:

- built-in and linked-package registry probing completed successfully
- `ModulatedGain` now probes cleanly during the normal registry scan
- no unresolved `Smooth` vtable/probe error appeared

This was used as the reliable startup-adjacent registry proof in this environment because short-lived GUI startup log capture was not stable enough to treat as better evidence than the explicit CLI registry scan.

### Manual runtime smoke

Completed a limited runtime smoke in this environment:

- registry scan through the app binary/CLI path
- direct demo smoke run
- targeted runtime bundle

Not completed here:

- full interactive GUI workflow exercising:
  - launch
  - load graph
  - save/reload
  - manual apply-pending mutation
  - live hot reload
  - quit

That remains a follow-up item, but it did not block the automated runtime-core signoff for this phase.

### Historical cross-check

Cross-checked current behavior against:

- `docs/internal/CODE-AUDIT-TRACKER.md`

The prior hardening work for:

- transactional rebuild/reload
- audio snapshot safety
- conservative hot reload
- export/capture/update regressions

still appears intact based on the current passing targeted lanes.

## Findings

### 1. `test_demo_graphs` is now a trustworthy and diagnosable runtime smoke lane

- Severity: `fixed`
- Workstream: `D — Harness / Headless Runtime Signals`
- Evidence:
  - `ctest --test-dir build --output-on-failure -R "test_demo_graphs"` passes
  - direct `./build/test_demo_graphs ./build/graphs` also passes
  - the harness now emits per-graph lifecycle checkpoints and supports narrow repro filtering
  - plugin/preset discovery now resolves relative to the built test binary instead of depending on caller cwd
- Why this matters:
  - the prior Phase 1 blocker was not actionable because the smoke lane failed under `ctest` while passing directly
  - the lane is now deterministic and produces enough diagnostic structure to localize future failures quickly

### 2. The built-in `modulated_gain.dylib` probe failure is resolved

- Severity: `fixed`
- Workstream: `A — Startup / Shutdown / Rebuild Lifecycle`
- Evidence:
  - `./build/vivid list-packages` now reports `Registry: probed ModulatedGain from modulated_gain.dylib`
  - direct demo smoke also probes `ModulatedGain` cleanly
  - no unresolved `Smooth` vtable/probe error appeared in the current registry scan paths
- Why this matters:
  - startup-adjacent operator inventory is clean again
  - the loader/runtime no longer carries a known built-in probe defect into Phase 2

### 3. No concrete regressions were found in the core rebuild / reload / hot-reload runtime contracts

- Severity: `note`
- Workstreams:
  - `A — Startup / Shutdown / Rebuild Lifecycle`
  - `B — Audio / Scheduler / GPU Coherence`
  - `C — Hot Reload And Runtime Churn`
- Evidence:
  - passing lanes:
    - `test_runtime_api`
    - `test_scheduler`
    - `test_audio_engine`
    - `test_hot_reload`
    - `test_audio_hot_reload`
    - `test_runtime_stress`
    - `test_hot_reload_stress`
    - `test_mixed_runtime_stability`
    - `test_capture_coordinator`
    - `test_export_pipeline`
    - `test_app_update_manager`
    - `test_hot_reloader_queue`
  - startup-adjacent registry scan succeeds through the app binary path
  - demo smoke and runtime smoke lanes are green in the current build tree

### 4. Headless GPU-demo execution coverage is still environment-limited

- Severity: `defer`
- Workstreams:
  - `B — Audio / Scheduler / GPU Coherence`
  - `D — Harness / Headless Runtime Signals`
- Evidence:
  - current headless smoke reports `No GPU — GPU-only graphs will be skipped`
  - `test_demo_graphs` therefore validates control/audio/runtime behavior here, but not successful GPU-only graph ticking
  - later windowed follow-up work confirmed `rich_text_demo.json` can run and capture a screenshot successfully in the normal GUI path
- Current classification:
  - not a Phase 1 blocker for this environment because the smoke lane itself is now trustworthy and explicit about what it skips
  - the remaining gap is GPU-capable verification coverage, not a confirmed runtime-core crash

## Required Fixes For Release

### Immediate release blockers

- None remaining from Phase 1.

### Required before release, but not currently classified as standalone blockers

1. complete a fuller manual runtime smoke once interactive validation is practical
2. keep runtime doc contracts current if a later runtime fix changes observable behavior

## Deferred Follow-Ups

Still explicitly deferred from this phase:

- headless GPU-demo execution coverage beyond the current skip classification
- deeper GPU-capable automation for GPU-only demo verification
- `test_semantic_tags` failures
- inspector/UI quality work
- broader package/ecosystem conformance

If any of those expose runtime-core defects later, they should be reopened explicitly rather than folded into this phase silently.

## Signoff Status

- `pass with defer`

Reason:

- the original Phase 1 blocker is fixed
- the `ModulatedGain` probe failure is fixed
- the targeted runtime-core validation bundle is green
- some GPU-specific and manual-interactive validation follow-ups remain, but they do not currently justify holding Phase 1 open
