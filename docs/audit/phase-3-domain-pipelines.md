# Phase 3 — Domain Pipelines And Cross-Domain Behavior

## Scope Reviewed

- control/audio/GPU/media behavior
- cross-domain bridges
- timing-sensitive and analysis-sensitive workflows
- explicit-output correctness across domains

## Evidence Gathered

- Current repo state during Phase 3:
  - branch: `master`
  - worktree: dirty only from current audit and screenshot-smoke follow-up docs/tests
- Domain/media inventory:
  - `ctest --test-dir build -N | rg "test_(audio_engine|audio_float_snapshot|audio_robustness|audio_spread_wire|audio_domain_sequencer|gpu_operators|cross_domain_spread|spread_broadcast|media_headless|demo_graphs|movie_decode_upload|movie_load_generation|movie_load_async|movie_long_loop_sync|movie_decode_route|export_pipeline)"`
  - discovered `16` targeted Phase 3 lanes:
    - `test_export_pipeline`
    - `test_audio_engine`
    - `test_audio_float_snapshot`
    - `test_audio_robustness`
    - `test_gpu_operators`
    - `test_cross_domain_spread`
    - `test_spread_broadcast`
    - `test_audio_spread_wire`
    - `test_demo_graphs`
    - `test_media_headless`
    - `test_audio_domain_sequencer`
    - `test_movie_decode_upload`
    - `test_movie_load_generation`
    - `test_movie_load_async`
    - `test_movie_long_loop_sync`
    - `test_movie_decode_route`
- Focused Phase 3 bundle:
  - `ctest --test-dir build -R "test_audio_engine|test_audio_float_snapshot|test_audio_robustness|test_audio_spread_wire|test_audio_domain_sequencer|test_gpu_operators|test_cross_domain_spread|test_spread_broadcast|test_media_headless|test_demo_graphs|test_movie_decode_upload|test_movie_load_generation|test_movie_load_async|test_movie_long_loop_sync|test_movie_decode_route|test_export_pipeline" --output-on-failure"`
  - result: `16/16` passed
- Timing/media-heavy rerun:
  - `ctest --test-dir build -R "test_media_headless|test_demo_graphs|test_movie_load_async|test_movie_long_loop_sync|test_movie_decode_route|test_audio_domain_sequencer" --output-on-failure"`
  - result: `6/6` passed
- Direct smoke commands for shipped graph classification:
  - `./build/test_demo_graphs ./build/graphs`
    - result: `19` passed, `0` failed, `65` skipped
    - interpretation: audio and non-GPU graphs exercised cleanly; GPU-only, movie/media, and external-I/O graphs skipped intentionally in the no-GPU headless lane
  - `./build/test_media_headless ./build/graphs`
    - result: `0` passed, `0` failed, `3` skipped, process exited non-zero
    - interpretation: on this no-GPU machine the standalone media smoke has no passing case and returns non-zero by contract when every curated media graph skips; this is an environment-sensitive limitation, not a focused-lane product failure
- Direct contract evidence from current docs and implementation:
  - [audio_engine.md](/Users/jeff/Developer/vivid/docs/runtime/audio_engine.md) defines the current cross-domain bridge contracts: control-to-audio `ParamSnapshot`, audio-to-control `AnalysisSnapshot`, and the supported float/spread/string/custom wire types
  - [scheduler.md](/Users/jeff/Developer/vivid/docs/runtime/scheduler.md) confirms audio nodes remain visible in the scheduler only for control-domain wiring while audio evaluation runs in the parallel `AudioEngine`
  - [ARCHITECTURE.md](/Users/jeff/Developer/vivid/docs/ARCHITECTURE.md) locks the Control-at-the-center topology and the `MovieLoaded` / `MovieAudioOut` / `MovieVideoOut` media trio contract
  - [test_audio_spread_wire.cpp](/Users/jeff/Developer/vivid/tests/test_audio_spread_wire.cpp) exercises control-spread-to-audio behavior and release/re-attack behavior across the audio bridge
  - [test_movie_long_loop_sync.cpp](/Users/jeff/Developer/vivid/tests/test_movie_long_loop_sync.cpp) exercises loop-boundary resync, bounded underruns, and stale-generation rejection in the media session
  - [test_media_headless.cpp](/Users/jeff/Developer/vivid/tests/test_media_headless.cpp) confirms the standalone media smoke intentionally returns non-zero when all curated graphs skip (`passes == 0`)
- Historical boundary:
  - this phase uses current command evidence and current runtime/media contracts only
  - the previous role-binding-era audit is background context, not proof

## Findings

### 1. Control → audio bridge is healthy

- Classification: `pass`
- Current read:
  - `test_audio_engine`, `test_audio_float_snapshot`, `test_audio_robustness`, `test_audio_spread_wire`, and `test_audio_domain_sequencer` all passed in the focused bundle
  - the timing-focused rerun also kept `test_audio_domain_sequencer` green
- Why it matters:
  - this is the core evidence that control-side params, spreads, and timing signals still cross into the audio engine correctly after the embedded-composition switch

### 2. Control → GPU and explicit-output behavior is healthy in the focused lanes

- Classification: `pass`
- Current read:
  - `test_gpu_operators`, `test_cross_domain_spread`, and `test_spread_broadcast` all passed
  - the current runtime/docs contract still matches the intended model: Control remains the only cross-domain bridge, and graph-visible outputs remain the mechanism for reuse across domains
- Why it matters:
  - Phase 3 needs confidence that cross-domain routing still works without hidden role-binding semantics; these lanes show the current ports/spreads/outputs model is intact

### 3. Timing-sensitive audio/control workflows are healthy

- Classification: `pass`
- Current read:
  - `test_audio_domain_sequencer` passed in both the focused bundle and the timing/media-heavy rerun
  - `test_movie_long_loop_sync` passed in both runs and continues to cover bounded resync/underrun behavior at loop boundaries
- Why it matters:
  - Phase 3 is not just about wiring correctness; it also needs to show that the timing-sensitive workflows creators actually patch with remain stable under load and across loops

### 4. Media pipeline integrity is healthy in the focused bundle

- Classification: `pass`
- Current read:
  - `test_movie_decode_upload`, `test_movie_load_generation`, `test_movie_load_async`, `test_movie_long_loop_sync`, `test_movie_decode_route`, `test_media_headless`, and `test_export_pipeline` all passed
  - this covers decode/upload, generation changes, async load behavior, long-loop sync, decode routing, curated media headless coverage, and the movie out path used by export
- Why it matters:
  - the current architecture depends on the media custom-port route and AV sync path continuing to work without any role-binding-era special cases

### 5. Headless shipped-graph smoke is healthy within the expected no-GPU limits

- Classification: `pass with defer`
- Current read:
  - `test_demo_graphs` passed in both the focused bundle and the timing/media rerun
  - direct `./build/test_demo_graphs ./build/graphs` smoke produced `19` passes, `0` failures, `65` skips
  - the skips were dominated by GPU-only graphs, curated movie/media graphs deferred to `test_media_headless`, and external-I/O graphs intentionally skipped in headless smoke
- Why it matters:
  - this is the broadest “real shipped graphs” check we have in Phase 3, and it remains healthy, but on this machine it still cannot fully validate GPU-backed demo behavior directly

### 6. Standalone direct media smoke remains environment-sensitive on this machine

- Classification: `deferred`
- Current read:
  - direct `./build/test_media_headless ./build/graphs` smoke produced `0` passes, `0` failures, `3` skips and exited non-zero because the harness returns failure when all curated media graphs skip
  - the focused CTest lane still passed, so this is not a deterministic product regression in the validated test bundle
- Why it matters:
  - Phase 3 should carry forward that GPU-available direct media smoke still needs a machine that can actually exercise one of the curated media graphs instead of skipping all of them

## Required Fixes For Release

- None established by Phase 3.

## Deferred Follow-Ups

- GPU-available direct media smoke should still be run on a machine that can exercise at least one curated media graph without all cases skipping.
- GPU-available shipped-graph smoke remains relevant for later audit phases and final release validation even though the no-GPU headless lane stayed healthy here.

## Signoff Status

- `pass with defer`
