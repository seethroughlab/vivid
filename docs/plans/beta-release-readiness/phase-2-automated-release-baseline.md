# Phase 2: Automated Release Baseline

## Goal

Establish a clean automated baseline before human review begins. Human listening, visual inspection, and beginner-doc review should happen after the obvious mechanical failures are known.

## Inputs

- Current branch and commit hash
- Debug and RelWithDebInfo builds
- Existing CTest suite
- `docs/testing/MOVIE-PLAYBACK-GO-NO-GO.md`
- `docs/testing/STABILITY-STRESS-TESTS.md`
- `docs/testing/UI-SCREENSHOT-SMOKE.md`

## Steps

1. Configure and build Debug.
2. Configure and build RelWithDebInfo.
3. Run the normal CTest baseline with output on failure.
4. Run `test_demo_graphs` against all sample graphs.
5. Run `UI_SMOKE`.
6. Run `GUI_SMOKE` on a machine that can launch the GUI lane.
7. Run `GUI_ENV` when package/environment setup is available.
8. Run movie playback go/no-go:
   - Automated tests from `MOVIE-PLAYBACK-GO-NO-GO.md`
   - Runtime diagnostics gate
   - Manual movie playback gate, if this phase is being used for final signoff
9. Run stability stress tests from `STABILITY-STRESS-TESTS.md`.
10. Run the opt-in soak target before final beta signoff.
11. Record all failures in the beta readiness checklist with blocker classification.

## Pass/Fail Criteria

Pass when all non-environment automated gates pass, and every skip has a documented reason that is not part of the first-run beta path.

Fail on crash, hang, failed graph load, missing core operator, WebGPU validation error, broken movie playback gate, stability stress failure, or unexplained skip in a core beginner path.

## Evidence to Record

- Commit hash
- Build type
- Commands run
- Test output summaries
- GUI smoke artifact paths
- Movie playback gate results
- Stress/soak duration and result
- Failure links or follow-up task IDs

## Exit Criteria

Phase 2 exits when automated release health is either green or fully triaged. Blocking automated failures must be fixed before Phase 6 pilot sharing.
