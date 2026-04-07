# Phase 5: Cleanup and Verification

## Summary

Phase 5 removes obsolete lane transport code and finishes the documentation, package, graph/query, and test updates for the clean-break redesign. After this phase, the codebase should no longer contain the old vector-staging or fixed-snapshot lane model except in archived documentation or intentionally historical notes.

This phase must leave the repo fully buildable and testable.

## Cleanup Changes

- Delete obsolete runtime lane staging structures and helpers:
  - fixed lane snapshot structs;
  - old vector lane staging fields;
  - old fixed-capacity lane pointer staging;
  - compatibility helpers for raw lane output ports.
- Remove or rewrite tests that encode the old ABI or old capacity limits.
- Update runtime and architecture docs to describe:
  - immutable lane views;
  - runtime-owned output builders;
  - `LaneBufferRef` transport;
  - cross-cadence snapshot/view lifetime;
  - GPU-backed lane storage and CPU readback boundaries.
- Update operator API docs and MCP opdev docs to remove raw `.data` / `.length` / `.capacity` output write examples.
- Update control-server query and graph snapshot docs for lane inspection from canonical lane refs.
- Update demo/test graph metadata only when needed for lane or GPU promotion policy. Avoid unrelated graph schema churn.
- Rebuild all seed operators and linked packages against the new ABI.

## Verification Changes

- Consolidate phase-specific tests into the normal test suite.
- Add final integration coverage for:
  - frame lane propagation;
  - audio direct lane routing;
  - frame-to-audio bridge;
  - audio-to-frame bridge;
  - GPU storage-buffer lane inputs;
  - control-server lane inspection;
  - package/demo graph loading with rebuilt operators.
- Add documentation checks where practical so old public lane output examples do not reappear in active docs.

## Non-Goals

- Do not introduce new lane features in this phase.
- Do not tune GPU promotion heuristics beyond the defaults and cases already tested in Phase 4.
- Do not perform unrelated graph schema cleanup.
- Do not keep old runtime code “just in case” if it only supports the removed lane ABI.

## Test Plan

- Run all phase-specific targeted tests one final time.
- Run full test suite:

```bash
ctest --test-dir build --output-on-failure
```

- Rebuild linked packages that participate in demo/package tests.
- Run package/demo graph tests after rebuilds.
- Search active docs and operator API examples for removed output-write guidance:
  - raw output `capacity` writes;
  - direct output lane `.data` mutation without builders;
  - old fixed `LaneSnapshot` guidance.
- Run control-server/query tests that inspect lane lengths and sample values.

## Exit Criteria

- No public operator context exposes old raw lane output ports.
- No active runtime path depends on fixed 64-float lane snapshots or fixed 1024 lane staging.
- No active docs teach old raw lane output writes.
- Seed operators, shared helpers, test operators, and linked packages build against the new ABI.
- Full test suite passes after package rebuilds.
- The overview and every phase page match the implemented final state.

## Assumptions and Defaults

- Phases 1 through 4 have already landed and are buildable.
- Archived docs may mention the old system if clearly historical.
- Active docs should describe only the clean-break lane model.
- [Overview](../lane-transport-redesign.md) remains the source for global invariants and phase ordering.
