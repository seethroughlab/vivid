# Phase 2: Automated Release Baseline

## Goal

Establish a clean automated baseline before human review begins. Human listening, visual inspection, and beginner-doc review should happen after the obvious mechanical failures are known.

## Inputs

- Current branch and commit hash
- Debug and RelWithDebInfo builds
- Existing CTest suite (143 tests)
- `docs/testing/MOVIE-PLAYBACK-GO-NO-GO.md`
- `docs/testing/STABILITY-STRESS-TESTS.md`
- `docs/testing/UI-SCREENSHOT-SMOKE.md`

## Procedure

### Step 1: Record baseline metadata

```
Date:           ____________________
Branch:         ____________________
Commit:         ____________________
macOS version:  ____________________
Hardware:       ____________________
```

### Step 2: Build Debug

```bash
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j$(sysctl -n hw.logicalcpu)
```

| Check | Result | Notes |
|-------|--------|-------|
| Configure completes without error | | |
| Build completes without error | | |
| Warnings reviewed (no new scary warnings) | | |

### Step 3: Build RelWithDebInfo

```bash
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build -j$(sysctl -n hw.logicalcpu)
```

| Check | Result | Notes |
|-------|--------|-------|
| Configure completes without error | | |
| Build completes without error | | |

All remaining steps run against the RelWithDebInfo build.

### Step 4: CTest full baseline

Run all 143 tests (excluding GUI_SMOKE and GUI_ENV which need separate env setup):

```bash
ctest --test-dir build --output-on-failure -E "test_ui_screenshot_smoke$|test_ui_screenshot_smoke_env"
```

| Check | Result | Notes |
|-------|--------|-------|
| Total tests run | /143 | |
| Tests passed | | |
| Tests failed | | |
| Tests skipped (with reason) | | |

Record every failure with test name, exit code, and first error line.

### Step 5: Demo graph smoke test

This is already part of Step 4 but called out explicitly since it's a critical beta gate.

```bash
ctest --test-dir build --output-on-failure -R "test_demo_graphs"
```

| Check | Result | Notes |
|-------|--------|-------|
| All 102 sample graphs load | | |
| All graphs tick 5 frames without crash | | |
| No WebGPU validation errors | | |
| No missing operator types | | |

### Step 6: Movie playback automated gate

Four tests from `MOVIE-PLAYBACK-GO-NO-GO.md`:

```bash
ctest --test-dir build --output-on-failure \
    -R "test_(audio_frame_bridge|movie_transport|movie_playback_modes|video_decode_worker)"
```

| Test | Result | Notes |
|------|--------|-------|
| test_audio_frame_bridge | | |
| test_movie_transport | | |
| test_movie_playback_modes | | |
| test_video_decode_worker | | |

### Step 7: Stability stress suite

Four stress tests plus the extended soak:

```bash
# Default stress suite (~30s each)
cmake --build build --target phase6_stress

# Extended soak (run before final beta signoff)
cmake --build build --target phase6_soak
```

| Test | Result | Duration | Notes |
|------|--------|----------|-------|
| test_runtime_stress | | | Save/mutate/reload cycles |
| test_hot_reload_stress | | | Audio hot-reload churn |
| test_package_stress | | | Package mutation on live graph |
| test_mixed_runtime_stability | | | Sustained mixed-domain ticking |
| phase6_soak (extended) | | | Extended pre-release soak |

### Step 8: UI_SMOKE (deterministic, no window)

```bash
ctest --test-dir build --output-on-failure -L '^UI_SMOKE$'
```

| Test | Result | Notes |
|------|--------|-------|
| test_ui_overlay_interactions | | |
| test_ui_editor_interactions | | |
| test_ui_widget_interactions | | |
| test_ui_screenshot_smoke (harness selftest) | | |

### Step 9: GUI_SMOKE (windowed, per-push)

Requires a display. Run locally, not in headless CI.

```bash
VIVID_ENABLE_UI_SCREENSHOT_SMOKE=1 \
    ctest --test-dir build --output-on-failure -L '^GUI_SMOKE$'
```

| Check | Result | Notes |
|-------|--------|-------|
| test_ui_screenshot_smoke passes | | |
| Semantic assertions verified | | |
| No GPU shader errors in log | | |
| Artifacts path (if failed) | | `build/.test_ui_screenshot_smoke/gui_smoke/artifacts/` |

### Step 10: GUI_ENV (package-dependent, optional)

Only run if external packages (`vivid-wavetable`, `vivid-sequencers`) are available and pre-built.

```bash
VIVID_ENABLE_UI_SCREENSHOT_SMOKE=1 \
VIVID_ENABLE_GUI_ENV_SMOKE=1 \
VIVID_GUI_ENV_PACKAGE_ROOT=<path-to-vivid-wavetable> \
VIVID_PACKAGE_PATHS=<parent-dir-containing-packages> \
    ctest --test-dir build --output-on-failure -L '^GUI_ENV$'
```

| Check | Result | Notes |
|-------|--------|-------|
| test_ui_screenshot_smoke_env passes | | |
| Skipped (packages not available) | | If skipped, note as N4 (env-dependent) |

## Blocker classification

### Blocking failures (beta NO-GO)

Per `phase-1/blocker-classes.md`, these are blockers:

- **B1** Crash in any test
- **B2** Hang / timeout in any test
- **B4** Graph load failure in `test_demo_graphs`
- **B5** Missing core operator
- **B6** WebGPU validation error
- **B7** Audio device lockup (test_audio_engine, stress tests)
- **B11** Broken save/load (test_runtime_stress, test_graph)

### Non-blocking skips

- **N4** Environment-dependent test skips (GPU adapter unavailable, no audio device) — document but do not block
- **N5** Package-dependent GUI_ENV skip — label and skip

## Pass/Fail Criteria

**Pass** when:
- Both Debug and RelWithDebInfo builds succeed
- All non-environment-dependent CTest tests pass
- All 102 demo graphs load and tick without crash
- Movie playback automated gate passes (4/4)
- Stability stress suite passes (4/4)
- UI_SMOKE passes (4/4 deterministic tests)
- GUI_SMOKE passes (semantic assertions verified)
- Every skip has a documented reason that is not on the beta-first-run path

**Fail** on:
- Build failure in either configuration
- Any test crash, hang, or assertion failure
- Demo graph load failure or missing operator
- WebGPU validation error
- Stress test timeout or memory error
- Unexplained skip in a core beginner path

## Evidence to Record

- [ ] Commit hash
- [ ] Build type (Debug, RelWithDebInfo)
- [ ] Build commands and any non-default flags
- [ ] Full CTest output summary (pass/fail/skip counts)
- [ ] Per-test results for all explicitly called-out gates
- [ ] GUI smoke artifact paths (if any failures)
- [ ] Movie playback gate results (4 tests)
- [ ] Stress/soak duration and result
- [ ] List of skipped tests with reasons
- [ ] Failure links or follow-up task IDs

## Exit Criteria

Phase 2 exits when automated release health is either green or fully triaged. Blocking automated failures must be fixed before Phase 6 pilot sharing.
