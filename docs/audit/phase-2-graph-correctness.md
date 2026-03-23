# Phase 2 — Graph Correctness And Mutation Surfaces

## Scope Reviewed

Primary graph and mutation surfaces reviewed in this phase:

- `src/runtime/graph.cpp`
- `src/runtime/graph.h`
- `src/runtime/runtime_api.cpp`
- `src/runtime/control_server.cpp`
- `src/runtime/runtime_command_sink.h`
- `src/runtime/undo_manager.cpp`
- `src/runtime/main.cpp` (`build_graph_snapshot`)
- `src/ui/graph_snapshot.h`

This phase focused on graph truth, mutation parity, undo/redo behavior, and snapshot consistency for runtime/UI consumers.

## Evidence Gathered

### Automated Phase 2 evidence bundle

Ran:

```bash
ctest --test-dir build --output-on-failure -R "test_graph|test_runtime_api|test_control_server|test_undo_mutation_types|test_role_binding_commands|test_role_binding_registry|test_graph_snapshot_contract"
```

Observed result:

- 7 of 7 matched tests passed

Passing lanes:

- `test_graph`
- `test_runtime_api`
- `test_control_server`
- `test_undo_mutation_types`
- `test_role_binding_commands`
- `test_role_binding_registry`
- `test_graph_snapshot_contract`

### Focused code-path review

Reviewed:

- graph schema load/save behavior in `Graph::load_from_string()` and `Graph::save_to_string()`
- runtime graph mutation paths in `RuntimeAPI`
- undo/redo integration in `RuntimeCommandSink` and control-server history handling
- graph snapshot production in `build_graph_snapshot()`
- control-server method coverage and mutation parity

### GPU sidecar follow-up

Ran the narrow repro path:

```bash
./build/test_demo_graphs ./build/graphs rich_text_demo
```

Observed result:

- `rich_text_demo.json` still skips on this machine with:
  - `No GPU — GPU-only graphs will be skipped`

Current classification:

- the earlier `Rich Text` GPU-path abort remains a tracked deferred issue
- it could not be reproduced or reclassified further in this environment because the headless test path still lacks a usable GPU adapter

## Findings

### 1. Control-server mutation parity now includes role bindings

- Severity: `fixed`
- Workstreams:
  - `live mutation surfaces`
  - `RuntimeAPI / control-server parity`
- Evidence:
  - `control_server.cpp` now exposes:
    - `set_role_binding`
    - `clear_role_binding`
  - both methods are tracked by the existing snapshot-based undo history
  - `tests/test_control_server.cpp` now covers:
    - successful remote bind
    - invalid bind rejection
    - undo/redo after bind
    - clear
    - undo/redo after clear
- Why this matters:
  - role bindings are persisted graph truth
  - remote clients using control server / MCP can now mutate them through the same main-thread command path as other graph edits

### 2. Graph snapshot contract coverage now includes role-binding graph truth

- Severity: `fixed`
- Workstreams:
  - `graph snapshot consistency`
  - `UI/runtime graph truth parity`
- Evidence:
  - `tests/test_graph_snapshot_contract.cpp` now explicitly covers:
    - `role_binding_snapshots`
    - `referenced_by`
  - the contract test now verifies host/target relationship data survives lookup as expected
- Why this matters:
  - role bindings are now part of the snapshot contract used by the inspector and reverse-reference UI
  - regressions in those fields now have direct test coverage

### 3. Core graph serialization and undo paths look healthy in the tested surfaces

- Severity: `note`
- Workstreams:
  - `graph serialization`
  - `undo/redo correctness`
  - `role binding graph truth`
- Evidence:
  - `test_graph` covers:
    - schema version handling
    - round-trip serialization
    - role-binding persistence
    - rejection of legacy `embedded_ops`
  - `test_role_binding_commands` covers:
    - validation
    - round-trip
    - undo/redo via `RuntimeCommandSink`
  - `test_undo_mutation_types` covers common undo/redo mutation classes
  - `test_control_server` is now green for both the older graph mutation surface and remote role-binding mutations
- Current read:
  - no concrete silent-corruption or undo-regression signal appeared in the current tested paths

### 4. The deferred `Rich Text` GPU-path issue is now best understood as a verification-gap issue

- Severity: `defer`
- Workstreams:
  - `GPU sidecar follow-up`
  - `verification infrastructure`
- Evidence:
  - filtered `rich_text_demo` still skips for lack of a headless GPU adapter in this environment
  - later follow-up work confirmed the normal windowed runtime path succeeds for `rich_text_demo.json`
- Current classification:
  - keep this as a later GPU/domain verification item
  - do not reopen Phase 1 from this machine alone

## Required Fixes For Release

### Immediate release blockers

- None remaining from Phase 2.

### Required before release, but not currently classified as standalone blockers

1. continue tracking deeper GPU-capable verification coverage in later domain/GPU audit work

## Deferred Follow-Ups

Still explicitly deferred from this phase:

- deeper GPU-capable automation for GPU-only demo verification
- broader domain/operator semantic-tag quality work
- inspector/UI visual-quality work beyond graph-truth correctness
- package/ecosystem concerns that do not change graph mutation correctness

## Signoff Status

- `pass with defer`

Reason:

- the Phase 2 blocker is fixed
- the graph snapshot contract gap is fixed
- the targeted graph/mutation evidence bundle is green
- the remaining GPU-specific issue is real enough to track, but not broad enough to keep Phase 2 open from this environment alone
