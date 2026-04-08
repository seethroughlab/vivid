# Follow-Up: Post-Remediation Review

## Summary

The latest re-evaluation shows that the lane transport remediation has moved forward, but lane-related hard crashes still block completion.

Current lane verification status:

- `cmake --build build` passes cleanly in the latest run.
- Lane-focused passing tests:
  - `test_string_ports`
  - `test_lane_capacity`
  - `test_lane_compaction`
  - `test_midi_input_expression`
- Lane-related failing tests:
  - `test_operator_sweep`

This page supersedes the status details in `followup-implementation-review.md`, but it does not replace the five phase pages.

## What Improved

- Active code no longer contains the old public `VividLanePort`, `VividStringLanePort`, `LaneSnapshot`, or `kMaxLaneCapacity` symbols in the searched runtime/operator/test paths.
- `VividFrameContext` lane arrays now document full `port_ordinal` indexing.
- `tests/ops/test_operator_sweep.cpp` now allocates lane stubs by total port count, not lane-port count.
- `test_lane_capacity` passes.
- `test_string_ports` passes.
- `test_lane_compaction` passes.
- Sampled lane-output code, including `MidiInput`, now uses atomic all-or-nothing builder commits for related lane output groups.

## Remaining Lane-Transport Blockers

- `test_midi_input_expression` still segfaults after the first `poly_shared` note assertions, during the second-note path. The test harness now allocates output lanes by full port ordinal, and `MidiInput` now checks all builder `resize()` calls before committing, so this needs a debugger or sanitizer pass rather than another broad contract guess.
- `test_operator_sweep` still segfaults after `euclidean_fr`. The prior lane-stub sizing hypothesis has been addressed in source, so the remaining crash needs fresh investigation from the current build.
- Runtime docs under `docs/runtime/` still have no tracked diff. If the final lane runtime/control behavior remains changed, update the matching runtime docs as part of the lane transport completion work.

## Out-of-Scope Failures Observed

The latest targeted run also included failures outside lane transport. They should be tracked separately unless a later investigation proves a lane-transport cause:

- `test_ui_overlay_interactions`
- `test_locale_catalog_parity`
- `test_undo_mutation_types`
- `test_team_workflow_regression`

The previous full-suite run also failed `test_semantic_tags` and `test_ui_screenshot_smoke_env`; those were not rerun in the latest targeted pass and are not lane transport blockers in this follow-up.

## Required Lane Remediation Plan

1. Debug `test_midi_input_expression` with a crash backtrace or sanitizer build. Focus on the transition after `TestFrameContext::reset_outputs()` and the second `run_frame()` in the `poly_shared` regression.
2. Debug the remaining `test_operator_sweep` segfault from the current build. Do not assume it is the lane stub allocation issue; that sizing fix is already present.
3. If either crash is caused by stale package/operator artifacts rather than lane runtime code, document the rebuild/cleanup requirement in the lane plan and adjust the test harness to avoid false positives.
4. Update the relevant `docs/runtime/*.md` file for final lane runtime behavior if the implementation changes any runtime-facing contract.
5. Refresh or close `followup-implementation-review.md` after the lane-specific fixes so future readers are not sent chasing already-fixed 1024-capacity and lane-compaction findings.

## Lane Verification Gate

The lane transport redesign is not complete until:

- `cmake --build build` passes.
- `test_midi_input_expression` does not segfault.
- `test_operator_sweep` does not segfault.
- `test_lane_capacity`, `test_string_ports`, and `test_lane_compaction` continue to pass.
- Any runtime docs affected by lane transport behavior are updated.
- Full `ctest --test-dir build --output-on-failure` has no lane-transport failures. Non-lane failures should be tracked in their own follow-up.

## Current Lane Test Results

Latest targeted command:

```bash
ctest --test-dir build --output-on-failure -R "test_ui_overlay_interactions|test_operator_sweep|test_undo_mutation_types|test_team_workflow_regression|test_locale_catalog_parity|test_midi_input_expression|test_lane_capacity|test_string_ports|test_lane_compaction"
```

Lane-related failures:

- `test_midi_input_expression`
- `test_operator_sweep`

Lane-focused passes:

- `test_string_ports`
- `test_lane_capacity`
- `test_lane_compaction`

## Assumptions and Defaults

- This page is the active corrective follow-up for lane transport only.
- The earlier follow-up remains useful historical context, but this page is the current lane-transport status source.
- Lane transport is no longer blocked by the earlier 1024-capacity/string-lane/compaction findings based on the latest targeted run, but the remaining hard crashes must still be fixed before the branch is considered lane-safe.
- [Overview](../lane-transport-redesign.md) remains the source for global invariants and phase ordering.
