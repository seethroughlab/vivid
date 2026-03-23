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

## Current Interpretation

This document remains historically accurate for the Phase 2 audit window, but it does not describe the current architecture one-to-one.

At the time of this phase:

- role bindings were part of the active graph model
- the control server and snapshot contract were extended to support that design

Since then, role bindings have been removed. The current architecture uses owned embedded composition for host-local modulation, ordinary ports for graph transport, and explicit outputs for cross-domain sharing. The findings below should therefore be read as audit history for the then-current design, not as present-day architectural requirements.

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

### 1. During this phase, control-server mutation parity was extended to include role bindings

- Severity: `fixed`
- Workstreams:
  - `live mutation surfaces`
  - `RuntimeAPI / control-server parity`
- Evidence:
  - during this phase, `control_server.cpp` exposed:
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
  - at the time of this phase, role bindings were persisted graph truth
  - this phase verified that remote clients using control server / MCP could mutate that then-current graph model through the same main-thread command path as other graph edits

### 2. During this phase, graph snapshot contract coverage was extended to include role-binding graph truth

- Severity: `fixed`
- Workstreams:
  - `graph snapshot consistency`
  - `UI/runtime graph truth parity`
- Evidence:
  - `tests/test_graph_snapshot_contract.cpp` explicitly covered the then-current fields:
    - `role_binding_snapshots`
    - `referenced_by`
  - the contract test now verifies host/target relationship data survives lookup as expected
- Why this matters:
  - during the role-binding design period, those fields were part of the snapshot contract used by the inspector and reverse-reference UI
  - this phase gave that historical contract direct test coverage

### 3. Core graph serialization and undo paths look healthy in the tested surfaces

- Severity: `note`
- Workstreams:
  - `graph serialization`
  - `undo/redo correctness`
  - `then-current role-binding graph truth`
- Evidence:
  - `test_graph` covers:
    - schema version handling
    - round-trip serialization
    - role-binding persistence in the then-current graph model
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

---

**Note (March 2026):** Role bindings were an intermediate design that has since been removed. The codebase now uses owned embedded composition for host-local modulation, ordinary ports for graph transport, and explicit outputs for cross-domain sharing. See `docs/EMBEDDED-OPERATOR-SLOTS.md` for the current architecture.
