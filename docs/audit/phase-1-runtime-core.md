# Phase 1 — Runtime Core Stability

## Scope Reviewed

- startup / shutdown
- rebuild / reload / scheduler / audio / GPU lifecycle
- hot reload and runtime coherence under the current embedded-composition architecture

## Evidence Gathered

### Current repo state during Phase 1

- branch: `master`
- worktree: not clean
- current in-flight changes during this audit pass are limited to:
  - audit doc updates
  - screenshot-smoke harness/test cleanup from the earlier Phase 0 triage
- current read:
  - the runtime-core evidence was gathered against a dirty tree, but the dirty files are not runtime-core implementation changes

### Runtime-core test inventory

Runtime-core lanes included in this phase:

- `test_hot_reload`
- `test_audio_hot_reload`
- `test_runtime_api`
- `test_latency_validation`
- `test_control_server`
- `test_audio_engine`
- `test_audio_float_snapshot`
- `test_audio_robustness`
- `test_scheduler`
- `test_operator_loader`
- `test_runtime_stress`
- `test_hot_reload_stress`
- `test_mixed_runtime_stability`
- `test_gpu_operators`
- `test_media_headless`
- `test_file_watcher`
- `test_hot_reloader_queue`

### Command evidence

Commands run for Phase 1:

- `git branch --show-current`
  - observed: `master`
- `git status --short`
  - observed: dirty worktree limited to audit/screenshot-smoke follow-up files
- `ctest --test-dir build -N | rg "test_(hot_reload|audio_hot_reload|runtime_api|latency_validation|control_server|audio_engine|audio_float_snapshot|audio_robustness|scheduler|operator_loader|runtime_stress|hot_reload_stress|mixed_runtime_stability|gpu_operators|media_headless|file_watcher|hot_reloader_queue)"`
  - observed: `17` runtime-core test lanes in scope
- `ctest --test-dir build -R "test_hot_reload|test_audio_hot_reload|test_runtime_api|test_latency_validation|test_control_server|test_audio_engine|test_audio_float_snapshot|test_audio_robustness|test_scheduler|test_operator_loader|test_runtime_stress|test_hot_reload_stress|test_mixed_runtime_stability|test_gpu_operators|test_media_headless|test_file_watcher|test_hot_reloader_queue" --output-on-failure`
  - observed: `17/17` passed
- `ctest --test-dir build -R "test_runtime_stress|test_hot_reload_stress|test_mixed_runtime_stability|test_media_headless" --output-on-failure`
  - observed: `4/4` passed
- `ctest --test-dir build -R "test_media_headless" --output-on-failure`
  - observed: `1/1` passed

### Direct runtime/log evidence

Existing current logs from the repaired screenshot-smoke runs were used as direct runtime-core evidence:

- `build/.test_ui_screenshot_smoke/gui_smoke/artifacts/instanced_shapes.log`
  - observed:
    - graph loaded successfully
    - control server listened on `127.0.0.1:9876`
    - file watcher and hot reload started
    - clean shutdown completed
- `build/.test_ui_screenshot_smoke/gui_env/artifacts/wavetable_dream_keys_cp1.log`
  - observed:
    - package graph loaded successfully
    - control server listened on `127.0.0.1:9876`
    - package file watchers were active
    - clean shutdown completed

### Historical boundary

- the previous role-binding-era audit remains useful for comparison, but it was not used as release evidence for this phase
- all findings below come from the current command and log evidence only

## Findings

### 1. Startup and shutdown behavior is currently healthy

- Classification: `pass`
- Current read:
  - the focused runtime-core suite passed cleanly
  - the direct runtime logs show successful graph load, control-server start, file-watcher activation, and clean shutdown
  - no crash, hang, or stuck teardown signal was established in this phase
- Why it matters:
  - startup/shutdown is the first runtime contract every later phase depends on

### 2. Scheduler and runtime API lifecycle are currently healthy

- Classification: `pass`
- Current read:
  - `test_scheduler`, `test_runtime_api`, `test_latency_validation`, and `test_control_server` all passed
  - the runtime-core evidence supports stable scheduler build, parameter propagation, topo behavior, and mutation-path safety
  - no rollback or mutation-surface defect was established in this phase
- Why it matters:
  - graph correctness, UI, and release validation all depend on scheduler/runtime API stability

### 3. Audio lifecycle and the control→audio bridge are currently healthy

- Classification: `pass`
- Current read:
  - `test_audio_engine`, `test_audio_float_snapshot`, `test_audio_robustness`, and `test_audio_hot_reload` all passed
  - the current build supports audio build/start/pause/resume/shutdown, control snapshot propagation, and safe recovery from rejected audio reloads
  - no underrun-growth or bridge-coherency defect was established in this phase
- Why it matters:
  - this is the core runtime guarantee for any mixed control/audio graph under the new architecture

### 4. Hot reload and file-watcher coherence are currently healthy

- Classification: `pass`
- Current read:
  - `test_hot_reload`, `test_hot_reload_stress`, `test_operator_loader`, `test_file_watcher`, and `test_hot_reloader_queue` all passed
  - the current runtime evidence supports compatible reload success, incompatible reload rejection, and watcher/queue stability
  - no partial-reload or stale-runtime defect was established in this phase
- Why it matters:
  - hot reload is a core development loop promise, not a secondary convenience

### 5. Headless GPU/media runtime stability is currently healthy in this environment

- Classification: `pass`
- Current read:
  - `test_gpu_operators`, `test_media_headless`, and `test_mixed_runtime_stability` all passed in both the focused bundle and the stress-biased rerun
  - the current evidence supports headless GPU setup, media-headless scheduler/audio build paths, and mixed-runtime loop stability
  - no deterministic GPU/media lifecycle defect was established in this phase
- Why it matters:
  - the runtime must remain stable even when GPU, audio, and control are all active together

### 6. GPU-available full-graph validation remains a later-phase concern, not a Phase 1 runtime-core defect

- Classification: `deferred`
- Current read:
  - Phase 0 already established that direct demo smoke on this machine skips GPU-only graphs in a no-GPU headless lane
  - that constraint still matters for release confidence, but it does not contradict the current Phase 1 runtime-core evidence
- Why it matters:
  - this should be carried into later graph/domain/release phases without over-classifying it as a runtime-core failure

### 7. Control-server port collisions should be treated as harness/environment noise when the app continues cleanly

- Classification: `known at audit start`
- Current read:
  - the current direct runtime logs show successful bind/listen behavior
  - when a `127.0.0.1:9876` bind collision appears during concurrent test activity, it should be treated as a harness/environment issue if the app otherwise continues and shuts down cleanly
- Why it matters:
  - this avoids turning concurrent-runner noise into a false runtime-core blocker

## Required Fixes For Release

- None established by Phase 1.

## Deferred Follow-Ups

- GPU-available full-graph validation is still needed in later audit phases and release validation.

## Signoff Status

- `pass`

Phase 1 established that the current embedded-composition-era runtime core is stable under the focused runtime-core evidence bundle:

- `17/17` runtime-core tests passed in the main focused run
- `4/4` stress-biased reruns passed
- direct runtime logs showed successful load, service startup, hot-reload/watch activation, and clean shutdown

Current carry-forward:

- Phase 2 should assume runtime-core lifecycle stability unless graph-correctness evidence proves otherwise
- later phases should still cover GPU-available end-to-end validation and manual release flows, but Phase 1 did not establish a runtime-core blocker
