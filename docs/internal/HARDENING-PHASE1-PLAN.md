# Phase 1 Hardening Plan: Lock In The New Runtime Guarantees

## Purpose

This plan turns Phase 1 of `docs/internal/HARDENING-ROADMAP.md` into a concrete implementation slice.

Goal: make the runtime guarantees established during the audit hard to regress by adding the missing automated coverage around the highest-risk transactional and lifecycle boundaries.

This is a hardening slice, not a feature slice.

## Scope

This phase covers four areas only:

1. runtime rebuild / rollback guarantees
2. export-path correctness guarantees
3. capture and update-manager failure/lifetime guarantees
4. regression harness cleanup needed to support those tests reliably

It does **not** include broader package-test runner work, UI contract expansion, or new feature work.

## Deliverables

By the end of Phase 1 we should have:

- direct automated regression tests for the audited runtime rollback paths
- direct automated regression tests for export output path + custom-port export registration
- explicit tests for capture-finalization failure reporting
- explicit tests for app-update worker lifetime behavior
- a small note in the audit tracker that Phase 1 hardening is complete

## Workstream A: Runtime Rollback And Refresh Regressions

### Goal

Make the runtime transactional guarantees hard to break accidentally.

### Implementation tasks

1. Add a focused regression group for destructive package mutations against live graphs:
   - install with missing operators present
   - link with missing operators present
   - rebuild while operators are active in the graph
   - unlink/uninstall while active graph nodes still reference the package
   - assert runtime ends in a coherent missing-operator or refreshed state, never half-live

2. Add failure-path assertions around snapshot/reload transactions:
   - malformed graph reload leaves prior graph/runtime intact
   - failed `apply_snapshot_json()` leaves source path, dirty state, and live graph intact
   - failed refresh after package mutation reports failure clearly and leaves runtime coherent

3. Add one explicit hot-reload regression for “rejected but safe” reloads:
   - descriptor-incompatible reload leaves old loader active
   - scheduler and audio operator versions remain aligned after failure

### Likely files

- `/Users/jeff/Developer/vivid/tests/test_runtime_api.cpp`
- `/Users/jeff/Developer/vivid/tests/test_control_server.cpp`
- `/Users/jeff/Developer/vivid/tests/test_hot_reload.cpp`
- `/Users/jeff/Developer/vivid/src/runtime/runtime_api.cpp` only if a test exposes residual inconsistency
- `/Users/jeff/Developer/vivid/src/runtime/control_server.cpp` only if a test exposes residual inconsistency

### Acceptance criteria

- rollback/failure paths are tested directly, not inferred from happy-path tests
- package mutation + refresh contracts are covered under active-graph conditions
- hot-reload failure leaves the prior operator live and aligned across domains

## Workstream B: Export Path Regression Coverage

### Goal

Turn the export fixes from “implemented” into “protected.”

### Implementation tasks

1. Add a focused export regression test or test fixture covering selected output path behavior:
   - chosen output path is used as final artifact location
   - sidecar runtime files are copied relative to that chosen path, not cwd

2. Add an export regression covering custom-port registration in standalone builds:
   - build/export a graph using at least one custom port type already present in core
   - assert the generated standalone registration includes the required custom type metadata
   - prefer a test that inspects generated artifacts rather than launching the exported app unless launch is cheap and deterministic

3. If needed, add a small fixture graph dedicated to export contract testing rather than reusing a larger demo graph.

### Likely files

- `/Users/jeff/Developer/vivid/tests/` new export-focused test
- `/Users/jeff/Developer/vivid/src/export/export_pipeline.cpp` only if tests uncover a remaining contract gap
- `/Users/jeff/Developer/vivid/src/export/export_pipeline.h`
- `/Users/jeff/Developer/vivid/graphs/` only if a dedicated minimal export fixture helps

### Acceptance criteria

- output destination correctness is asserted automatically
- custom-port registration for export is asserted automatically
- export no longer depends on manual verification for these audited fixes

## Workstream C: Capture And Update Lifecycle Regressions

### Goal

Protect the release-path fixes that are easy to regress because they sit outside the main graph loop.

### Implementation tasks

1. Extend capture regression coverage:
   - stop-recording returns `ok:false` when finalization fails
   - returned payload still includes enough path/diagnostic context to debug failure

2. Extend app-update-manager tests:
   - refresh thread does not outlive object destruction unsafely
   - repeated refresh calls do not accumulate detached workers
   - stale/failed fetch paths do not crash shutdown

3. Keep these tests lightweight and local; do not introduce network dependency.

### Likely files

- `/Users/jeff/Developer/vivid/tests/test_capture_coordinator.cpp`
- `/Users/jeff/Developer/vivid/tests/test_app_update_manager.cpp`
- `/Users/jeff/Developer/vivid/src/runtime/capture_coordinator.cpp` only if residual issues are found
- `/Users/jeff/Developer/vivid/src/runtime/app_update_manager.cpp` only if residual issues are found

### Acceptance criteria

- failure reporting is asserted for capture stop
- app-update lifetime safety is exercised by tests rather than trusted by inspection alone

## Workstream D: Harness And Invocation Cleanup

### Goal

Remove small testing-friction points that would otherwise make the new hardening coverage flaky or awkward to run.

### Implementation tasks

1. Fix or normalize the remaining working-directory-sensitive tests where practical, especially where the audit already noted invocation quirks.
2. Prefer fixture-local path resolution from executable/build-root inputs instead of implicit cwd assumptions.
3. Keep this scoped only to tests touched by Phase 1 coverage.

### Likely files

- `/Users/jeff/Developer/vivid/tests/test_control_server.cpp`
- `/Users/jeff/Developer/vivid/tests/test_demo_graphs.cpp` if needed
- test utility helpers if there are any shared path helpers worth extracting

### Acceptance criteria

- the new Phase 1 tests are reliable under direct invocation and `ctest`
- no new regression coverage depends on fragile cwd assumptions

## Execution Order

1. Workstream A: runtime rollback and refresh regressions
2. Workstream B: export path regression coverage
3. Workstream C: capture and update lifecycle regressions
4. Workstream D: harness cleanup only where needed to stabilize A-C

Reasoning:
- A protects the highest-value runtime guarantees
- B covers a recently fixed but still weakly protected path
- C covers outer-loop lifecycle fixes
- D supports the other three without becoming an open-ended cleanup pass

## Test Plan

Run at minimum:

```bash
ctest --test-dir build --output-on-failure -R "test_runtime_api|test_control_server|test_hot_reload|test_capture_coordinator|test_app_update_manager"
```

If export coverage gets its own target, add it explicitly to the Phase 1 gate.

## Completion Gate

Phase 1 is done when all of the following are true:

1. runtime rollback/rebuild failure behavior is directly regression-tested
2. export output path + custom-port registration behavior is directly regression-tested
3. capture failure reporting and app-update worker lifetime are directly regression-tested
4. the touched tests are stable under normal `ctest` execution
5. `docs/internal/CODE-AUDIT-TRACKER.md` records Phase 1 hardening as complete

## Non-Goals

Do not expand this phase into:

- package ecosystem redesign
- new UI editor features
- new operator ABI changes
- custom-port architecture redesign
- movie/media feature work

Those belong to later roadmap phases.

## Suggested Next Doc

After this phase is implemented, the next planning doc should be:

- `Phase 2 Hardening Plan: Finish Package-Test Contract Hardening`

That keeps the audit follow-through moving in the same order as the roadmap.
