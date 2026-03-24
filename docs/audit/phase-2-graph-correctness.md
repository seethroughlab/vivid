# Phase 2 — Graph Correctness And Mutation Surfaces

## Scope Reviewed

- graph serialization
- runtime mutations
- control-server mutation paths
- undo/redo
- embedded-slot / host-state correctness
- signal-port discipline
- snapshot consistency

## Evidence Gathered

### Current repo state during Phase 2

- branch: `master`
- worktree: not clean
- current in-flight changes during this audit pass are limited to:
  - audit doc updates
  - screenshot-smoke harness/test cleanup carried forward from Phase 0 triage
- current read:
  - the graph-correctness evidence was gathered against a dirty tree, but the dirty files are not graph/runtime mutation implementation changes

### Graph-correctness test inventory

Graph/mutation lanes included in this phase:

- `test_runtime_api`
- `test_control_server`
- `test_graph`
- `test_operator_destination_policy`
- `test_graph_snapshot_contract`
- `test_cross_domain_spread`
- `test_spread_broadcast`
- `test_child_op`
- `test_signal_port`
- `test_package_scope_resolver`
- `test_package_scope_registry`
- `test_string_ports`
- `test_undo_manager`
- `test_undo_mutation_types`
- `test_builtin_operators`

### Command evidence

Commands run for Phase 2:

- `git branch --show-current`
  - observed: `master`
- `git status --short`
  - observed: dirty worktree limited to audit/screenshot-smoke follow-up files
- `ctest --test-dir build -N | rg "test_(graph|runtime_api|control_server|undo_manager|undo_mutation_types|graph_snapshot_contract|signal_port|child_op|cross_domain_spread|spread_broadcast|string_ports|package_scope_registry|package_scope_resolver|operator_destination_policy|builtin_operators)"`
  - observed: `15` graph-correctness lanes in scope
- `ctest --test-dir build -R "test_graph|test_runtime_api|test_control_server|test_undo_manager|test_undo_mutation_types|test_graph_snapshot_contract|test_signal_port|test_child_op|test_cross_domain_spread|test_spread_broadcast|test_string_ports|test_package_scope_registry|test_package_scope_resolver|test_operator_destination_policy|test_builtin_operators" --output-on-failure`
  - observed: `15/15` passed
- `ctest --test-dir build -R "test_runtime_api|test_control_server|test_undo_manager|test_undo_mutation_types|test_graph_snapshot_contract|test_graph" --output-on-failure`
  - observed: `6/6` passed

### Direct graph-truth contract evidence

Current docs and implementation contracts used as direct graph-truth evidence:

- `docs/runtime/graph.md`
  - current contract: `Graph` remains the persistence source of truth
  - current contract: future schema versions are hard-rejected
- `docs/runtime/runtime_api.md`
  - current contract: topology mutations buffer through `apply_pending()`
  - current contract: `reload()` and `apply_snapshot_json()` are transactional restore-on-failure paths
- `src/runtime/main.cpp`
  - current contract: snapshots preserve graph truth even when an endpoint no longer resolves
- `src/runtime/control_server.cpp`
  - current contract: undo/redo reapply serialized snapshots through `apply_snapshot_json()`
  - current contract: failed undo application restores a safe baseline instead of leaving stale history in place

Current test evidence directly supporting those contracts:

- `tests/test_graph.cpp`
  - embedded-slot round-trip via `embedded_ops`
  - schema-version rejection
  - save/load and dangling-connection behavior
- `tests/test_runtime_api.cpp`
  - malformed snapshot apply restores graph, scheduler, source-path identity, and dirty-state correctly
- `tests/test_undo_mutation_types.cpp`
  - stale/missing-operator snapshots remain undoable through placeholder restoration
- `tests/test_string_ports.cpp`
  - mixed numeric→string and invalid string fan-in are rejected

### Historical boundary

- the previous role-binding-era audit remains useful for comparison, but it was not used as release evidence for this phase
- all findings below come from the current command and contract evidence only

## Findings

### 1. Graph serialization and load/save behavior are currently healthy

- Classification: `pass`
- Current read:
  - `test_graph` passed in both the focused Phase 2 bundle and the focused rerun
  - the current graph contract still holds:
    - `Graph` is the persistence source of truth
    - duplicate mutations reject cleanly
    - future schema versions hard-reject
    - save/load round-trips remain stable
    - embedded-slot state round-trips through `embedded_ops`
- Why it matters:
  - release trust depends on saved graphs meaning the same thing after reload

### 2. Runtime mutation buffering and transactional apply are currently healthy

- Classification: `pass`
- Current read:
  - `test_runtime_api` passed in both the focused Phase 2 bundle and the focused rerun
  - the current evidence supports:
    - buffered topology mutation through `apply_pending()`
    - `apply_snapshot_json()` rollback on malformed input
    - `reload()` / snapshot-apply preserving source-path identity and dirty-state correctly
  - no deterministic mutation-transaction defect was established in this phase
- Why it matters:
  - graph changes must never leave the live runtime half-applied or half-restored

### 3. Control-server mutation and undo/redo paths are currently healthy

- Classification: `pass`
- Current read:
  - `test_control_server`, `test_undo_manager`, and `test_undo_mutation_types` all passed in the focused run
  - the current evidence supports:
    - graph CRUD through the control-server path
    - undo/redo restoring serialized graph truth
    - history reset rules after graph load
    - safe recovery when undo applies a stale or placeholder-backed snapshot
  - no deterministic control-server mutation-path defect was established in this phase
- Why it matters:
  - remote/runtime control is a first-class workflow and must agree with the underlying graph truth

### 4. Embedded-slot and host-state correctness are currently healthy

- Classification: `pass`
- Current read:
  - `test_graph`, `test_child_op`, and `test_builtin_operators` all passed
  - the current evidence supports:
    - embedded ops parsed and serialized correctly
    - child state represented as host-local graph state
    - flat host params injected where intended from embedded operator state
  - no residual role-binding persistence assumption was established in this phase
- Why it matters:
  - the embedded-composition switch only holds if host-local state persists and restores cleanly

### 5. Signal-port, spread, and string-port discipline are currently healthy

- Classification: `pass`
- Current read:
  - `test_signal_port`, `test_cross_domain_spread`, `test_spread_broadcast`, `test_string_ports`, and `test_operator_destination_policy` all passed
  - the current evidence supports:
    - signal-port behavior staying distinct from host-local embedded composition
    - cross-domain spread handling remaining coherent
    - invalid string-port combinations rejecting cleanly
  - no deterministic port-discipline defect was established in this phase
- Why it matters:
  - this is the contract boundary between graph transport and host-local composition

### 6. Snapshot consistency currently matches the graph-truth contract

- Classification: `pass`
- Current read:
  - `test_graph_snapshot_contract` passed in both the focused Phase 2 bundle and the focused rerun
  - the implementation contract in `main.cpp` explicitly preserves graph truth even when endpoints no longer resolve
  - the current evidence supports snapshots that remain faithful to unresolved/broken graph state rather than silently normalizing it away
- Why it matters:
  - UI, diagnostics, undo, and remote inspection all depend on snapshots representing the real graph, not a cleaned-up approximation

### 7. Placeholder restoration after uninstall or stale snapshots is currently intentional and acceptable

- Classification: `pass`
- Current read:
  - `test_undo_mutation_types` passed through stale/missing-operator undo cases
  - the current behavior restores graph truth using missing-operator placeholders rather than dropping stale nodes or connections
  - Phase 2 established this as intentional contract behavior, not a correctness defect
- Why it matters:
  - preserving user graph truth through operator churn is safer than silently deleting unresolved state

## Required Fixes For Release

- None established by Phase 2.

## Deferred Follow-Ups

- None established by Phase 2.

## Signoff Status

- `pass`

Phase 2 established that the current embedded-composition-era graph and mutation surfaces are correct under the focused graph-correctness evidence bundle:

- `15/15` graph-correctness tests passed in the main focused run
- `6/6` mutation/snapshot reruns passed
- current docs, implementation notes, and tests all align on the same graph-truth contract:
  - transactional apply/reload
  - embedded-slot persistence
  - undo/redo through serialized snapshots
  - snapshots preserving unresolved graph truth

Current carry-forward:

- Phase 3 should assume graph persistence and mutation correctness unless domain-pipeline evidence proves otherwise
- later phases should still cover end-to-end release validation, but Phase 2 did not establish a graph-correctness blocker
