# Phase 6 Stability Stress Tests

## Purpose

These tests are the Phase 6 long-run reliability lane.
They are not feature demos. They are small, repeatable seam checks for the runtime paths that were hardened in earlier phases.

## Default Automated Stress Suite

Build the stress executables:

```bash
cmake --build build --target test_runtime_stress test_hot_reload_stress test_package_stress test_mixed_runtime_stability
```

Run the default Phase 6 suite:

```bash
ctest --test-dir build --output-on-failure -R "test_runtime_stress|test_hot_reload_stress|test_package_stress|test_mixed_runtime_stability"
```

Or use the convenience target:

```bash
cmake --build build --target phase6_stress
```

## Stress Lanes

### `test_runtime_stress`

Covers repeated runtime rebuild and rollback behavior:
- save / mutate / reload cycles
- successful and failed `apply_snapshot_json(...)`
- successful and failed `reload()`
- source-path and reload-serial stability under churn

### `test_hot_reload_stress`

Covers repeated audio hot-reload churn:
- compatible reload loops
- incompatible reload rejection
- scheduler/audio alignment after repeated cycles
- preservation of the previously active operator after rejection

Canonical fixtures:
- `tests/operators/audio_reload_v1.cpp`
- `tests/operators/audio_reload_v2.cpp`
- `tests/operators/audio_reload_v3.cpp`
- `tests/operators/audio_reload_incompatible.cpp`

### `test_package_stress`

Covers repeated package mutation on a live graph with transactional restore around each mutation:
- link
- rebuild
- unlink
- relink
- missing-operator fallback and recovery
- repeated runtime restore after package churn

Canonical fixture:
- synthetic live package generated inside `tests/test_package_stress.cpp`

### `test_mixed_runtime_stability`

Covers sustained mixed-domain ticking:
- control + audio + GPU graph stepping
- teardown after repeated ticks
- bounded headless behavior using the mixed-runtime stability graph

Canonical fixture:
- `tests/graphs/test_mixed_runtime_stability.json`

Note:
- this test can skip on machines where a headless WebGPU adapter is unavailable

## Opt-In Soak Target

For longer local or pre-release validation, use:

```bash
cmake --build build --target phase6_soak
```

This runs the mixed-runtime stability executable in extended mode.
It is intentionally separate from the default suite so runtime cost stays explicit.

## Interactive Stability Lane

Use this short manual checklist for the parts that are still hard to validate headlessly:

1. Open a mixed graph and let it run for an extended interactive session.
2. Perform repeated graph edits.
3. Perform small package mutations from the package browser or API.
4. Trigger hot reload repeatedly.
5. Confirm:
   - the editor remains responsive
   - graph truth stays visible, including broken-wire states
   - reload feedback remains accurate
   - package UI remains usable after churn
   - the app exits cleanly without hanging

## Notes

- The package stress lane is about lifecycle coherence under churn, not exhaustive verification of every package operator's DSP output.
- Exact package output refresh for rebuild/relink is already covered by targeted package/control-server regression tests.
- If a stress lane starts failing, prefer fixing the runtime contract first instead of weakening the lane unless the assertion is duplicating better-targeted coverage elsewhere.
