# Follow-Up: Implementation Review

> **Superseded.** The findings in this document have been addressed. See `followup-post-remediation-review.md` for current status.

## Summary

The first implementation pass builds, but it does not yet satisfy the clean-break lane transport target.

Current verification status:

- `cmake --build build` passes.
- `ctest --test-dir build --output-on-failure` currently fails 8 of 128 tests.
- The new operator ABI shape exists, but critical runtime paths still preserve old fixed limits and transitional behavior.

This document is corrective follow-up for the active redesign. It does not replace the five phase pages.

## Blocking Findings

- A hidden 1024-lane cap remains in `kMaxLaneCapacity`, output builders, string-lane buffers, and `AudioFrameBridge` slots. This means the implementation is still bounded by the old transport limit behind the new API.
- Lane output indexing is inconsistent. Public comments imply lane-port ordinal indexing, while runtime and many operators use full port ordinal indexing.
- Some operators commit lane outputs even when not all related builder `resize()` calls succeed. This can publish partial or stale lane groups.
- Audio lane compaction is failing, which indicates lane identity/state does not survive the new bridge and `LoopBased` audio path correctly.
- Unrelated clone/package UI changes are included in the same uncommitted set and currently fail tests. They need to be repaired or split out before evaluating lane transport as complete.
- Runtime docs under `docs/runtime/` were not updated even though runtime behavior changed.

## Required Remediation Plan

- Lock the lane output indexing contract. Recommended default: use full port ordinal indexing everywhere, because current runtime/operator code already follows it. Update public comments, tests, and helper stubs to match.
- Replace `kMaxLaneCapacity` with `max_lane_elements` policy. Runtime buffers should be sized from graph/runtime policy instead of the old constant.
- Make builder failure atomic per operator output group. If any required builder returns null, skip the entire lane output group and do not commit partial output.
- Preserve lane provenance and identity through `LaneBuffer`, bridge slots, and `VividLaneView::lane_set_id`, then fix `test_lane_compaction`.
- Separate or repair unrelated clone/package UI changes before treating this as the lane transport implementation.
- Update the matching `docs/runtime/*.md` files to describe the final runtime behavior.

## Verification Gate

The redesign is not complete until:

- `test_midi_input_expression` does not segfault.
- `test_operator_sweep` does not bus-error.
- `test_lane_compaction` passes.
- `test_lane_capacity` or a new test verifies frame and bridge transport above 1024 lanes.
- `test_string_ports` or a new test verifies string lanes above 1024 entries.
- `test_ui_overlay_interactions`, `test_undo_mutation_types`, `test_team_workflow_regression`, and `test_semantic_tags` either pass or are explicitly split out of the lane branch.
- Full `ctest --test-dir build --output-on-failure` passes.

## Assumptions and Defaults

- This page is an evaluative follow-up, not a replacement for the phase plan.
- The follow-up remains active until the verification gate passes.
- The recommended indexing default is full port ordinal indexing unless the implementation is deliberately changed and all docs/tests/operators are updated together.
- [Overview](../lane-transport-redesign.md) remains the source for global invariants and phase ordering.
