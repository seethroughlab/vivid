# Post-Cutover Legacy Removal Plan

## Summary

Remove the superseded wavetable production stack after the hard cutover is complete. The goal is for the active repo surface to read as if `WavetableLayer` had always been the production wavetable path, with legacy material retained only in archives where historically useful.

## Removal Scope

This plan covers removal of superseded wavetable production surfaces, including:

- legacy wavetable operators that were replaced as the recommended production path
- legacy production-oriented modules built around `WavetableOsc + VoiceMixer`
- active docs/examples that still teach the old stack as the preferred recipe
- old performance fixtures once replacement fixtures are established

This plan does not remove legacy-only advanced behavior until there is an explicit replacement or a clear archival decision for that behavior.

## Cleanup Requirements

- Migrate or archive all active graphs/examples that still depend on the superseded production stack.
- Remove old operators from active package catalog recommendations and active docs/examples.
- Move any historically useful legacy guidance into archived docs only.
- Ensure benchmark and listening fixtures now point only to `WavetableLayer`-based content.

## Compatibility Stance

- There is no indefinite support window for the superseded production path.
- Archived docs may mention old operators.
- Active docs may not present old operators as recommended production surfaces.
- If a legacy surface remains for excluded advanced features, it must be clearly limited to that role and excluded from production guidance.

## Dependencies

- [Phase 6: Hard Cutover and Legacy Status](./phase-6-hard-cutover-and-legacy-status.md)

## Test Plan

- Grep active docs/examples for old production-path guidance and remove remaining hits.
- Verify package catalog/browser recommendations no longer point at superseded operators.
- Verify benchmark fixtures and reference instruments no longer depend on the removed stack.
- Re-run package tests and benchmark fixtures after the removal pass to confirm the active path remains intact.

## Acceptance Criteria

- Old operators are absent from active docs/examples as recommended production surfaces.
- Old operators are absent from package catalog recommendations.
- Old performance fixtures are removed or archived.
- The active repo reads as though `WavetableLayer` has always been the production wavetable path.

## Not In This Phase

- No new renderer features.
- No reopening the hard-cutover decision.
- No long-term dual-track support model.

## Assumptions and Defaults

- Removal happens after the hard cutover, not before it.
- Archives preserve historical context; active surfaces should not.
- The goal is convergence, not permanent compatibility clutter.
