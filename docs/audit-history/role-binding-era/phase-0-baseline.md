# Phase 0 — Audit Baseline And Triage Setup

## Scope Reviewed

Phase 0 establishes the audit baseline only. It does not attempt subsystem review or code-quality judgment.

This pass answers:

- what is already known broken vs newly discovered
- what is passing, failing, or environment-sensitive today
- what counts as a release blocker
- where later phase findings should be recorded

Primary reference sources used in this baseline:

- `docs/internal/CODE-AUDIT-TRACKER.md`
- `docs/release/README.md`
- `docs/release/RELEASE-CHECKLIST.md`
- `docs/testing/README.md`
- `docs/testing/STABILITY-STRESS-TESTS.md`
- `docs/INTERFACE.md`
- `docs/runtime/*.md`

## Evidence Gathered

### Current automated baseline

- Enumerated the current `ctest` inventory: 84 registered tests
- Ran full baseline:
  - `ctest --test-dir build --output-on-failure`
- Re-ran focused failing tests:
  - `ctest --test-dir build --output-on-failure -R "test_demo_graphs|test_semantic_tags"`
- Re-ran demo smoke directly:
  - `./build/test_demo_graphs ./build/graphs`

### Current documentation baseline

- Release operations:
  - `docs/release/README.md`
  - `docs/release/RELEASE-CHECKLIST.md`
- Testing references:
  - `docs/testing/README.md`
  - `docs/testing/STABILITY-STRESS-TESTS.md`
- Historical audit context:
  - `docs/internal/CODE-AUDIT-TRACKER.md`

### Current repo state

- Audit start worktree is not clean
- Unstaged files at baseline:
  - `src/runtime/gpu_context.h`
  - `src/ui/node_graph.cpp`
  - `src/ui/node_graph_draw.cpp`

## Known Issues At Audit Start

### 1. `test_demo_graphs` fails under `ctest`

- Classification: `known at audit start`
- Evidence:
  - failed in the baseline `ctest` run with `Subprocess aborted`
  - direct binary invocation exited cleanly in the same workspace
- Current interpretation:
  - treat as environment-sensitive or harness-sensitive until proven otherwise
  - still release-relevant because demo smoke is meant to be a high-signal baseline lane

### 2. `test_semantic_tags` fails with invalid tag definitions

- Classification: `known at audit start`
- Evidence:
  - baseline `ctest` run failed with 15 invalid tags
- The current failures are concrete and deterministic, not speculative

### 3. Inspector UI quality remains a known weak area

- Classification: `known at audit start`
- Evidence:
  - recent UI work reduced overlap problems, but inspector quality and layout resilience are still being actively refined
- Reference:
  - `docs/INSPECTOR-UI-AUDIT-PLAN.md`

### 4. Some verification paths are intentionally environment-sensitive

- Classification: `known at audit start`
- Current examples:
  - movie/media graphs are deferred from `test_demo_graphs` to `test_media_headless`
  - external I/O graphs are skipped in the headless demo-smoke lane
  - some GPU-sensitive paths can skip depending on available runtime environment

## Current Verification Surface

### Trusted automated surfaces

These passed in the baseline run and are good starting evidence:

- Runtime core
  - `test_hot_reload`
  - `test_audio_hot_reload`
  - `test_export_pipeline`
  - `test_runtime_api`
  - `test_audio_engine`
  - `test_scheduler`
  - `test_graph`
  - `test_runtime_stress`
  - `test_hot_reload_stress`
  - `test_package_stress`
  - `test_mixed_runtime_stability`
- UI and editor contracts
  - `test_theme_loader`
  - `test_overlay_layouts`
  - `test_graph_snapshot_contract`
  - `test_inspector_layout`
  - `test_ui_overlay_interactions`
  - `test_ui_arch_guard`
  - `test_text_edit`
- Graph mutation and bindings
  - `test_undo_manager`
  - `test_undo_mutation_types`
  - `test_bound_control_instance`
  - `test_role_binding_registry`
  - `test_role_binding_commands`
  - `test_signal_port`
- Packages and ecosystem
  - `test_operator_loader`
  - `test_operator_creator`
  - `test_package_compiler`
  - `test_package_catalog`
  - `test_package_manager`
  - `test_package_scope_resolver`
  - `test_package_scope_registry`
  - `test_package_scaffolder`
  - `test_package_update_logic`
  - `test_package_test_runner`
  - `test_package_contract_ecosystem`
- Media / MIDI / analysis
  - `test_media_headless`
  - `test_midi`
  - `test_midi_file_parser`
  - `test_midi_file_player`
  - `test_movie_decode_upload`
  - `test_movie_load_generation`
  - `test_movie_load_async`
  - `test_media_clock`
  - `test_output_analyzer`

### Weak or environment-sensitive surfaces

- `test_demo_graphs`
  - currently not trustworthy enough to treat as a clean release signal
- headless smoke coverage for graphs
  - intentionally skips external I/O and defers movie/media graphs
- manual UI quality
  - automated layout/interaction tests exist, but visual polish and workflow quality still require manual review

### Manual smoke flows that matter for release

These should stay in the release audit loop even when automation passes:

- launch and shutdown
- load a graph
- save and reload a graph
- edit parameters in the inspector
- package scan/load and linked-package rebuild flow
- one representative GPU demo
- one representative audio demo

The main supporting docs for those manual checks are:

- `docs/release/RELEASE-CHECKLIST.md`
- `docs/testing/MANUAL-TEST-CATALOG.md`
- `docs/testing/STABILITY-STRESS-TESTS.md`

## Release Blocker Rubric

Treat any of the following as a release blocker unless there is an explicit written decision otherwise:

1. Crash, hang, data loss, or graph corruption
2. Broken save/load/reload or snapshot identity
3. Broken package/operator loading or rebuild flow in core workflows
4. Broken export or release/update path
5. Major unusable UI workflow
   - editor cannot be used reliably
   - inspector interaction is materially broken
   - graph truth is hidden or misleading

Non-blocking issues can still be important, but they should be tagged as:

- `required before release`
- or `deferred`

Later phases should use that same language consistently.

## Audit Phase Map

### Phase 1 — Runtime Core Stability

- startup / shutdown
- rebuild / reload / scheduler / audio / GPU lifecycle
- hot reload and runtime coherence

### Phase 2 — Graph Correctness And Mutation Surfaces

- graph serialization
- runtime mutations
- control-server mutation paths
- undo/redo
- historical role-binding graph model during the earlier Phase 2 audit window

### Phase 3 — Domain Pipelines And Cross-Domain Behavior

- control/audio/GPU/media behavior
- domain bridges
- timing-sensitive and analysis-sensitive workflows

### Phase 4 — UI And Interaction Audit

- node graph editing
- inspector system
- overlays and choosers
- session/variation workflows
- layout/readability/interaction resilience

### Phase 5 — Operator/Package/Ecosystem Audit

- loader and ABI surfaces
- package lifecycle
- metadata fidelity
- package authoring and extension workflows

### Phase 6 — Export, Release Surfaces, And Final Readiness

- export pipeline
- app update path
- demo graphs and shipped examples
- release checklist alignment
- final blocker/defer decision

## Required Fixes For Release

At baseline, these are the known required-fix candidates:

1. restore a trustworthy demo-smoke lane
2. resolve or intentionally reclassify the current semantic-tag failures
3. complete the broader inspector/UI audit before release signoff

These are starting candidates, not final phase findings.

## Deferred Follow-Ups

- None from Phase 0 itself beyond carrying forward the known-at-start issues above.

## Signoff Status

- `pass with defer`

Phase 0 is complete because:

- the release blocker rubric is now explicit
- the baseline documents exist
- current known issues are captured once in one place
- the audit phase order is frozen
- later phases now have a clean contract for recording findings

---

**Note (March 2026):** Role bindings were an intermediate design that has since been removed. The codebase now uses owned embedded composition for host-local modulation, ordinary ports for graph transport, and explicit outputs for cross-domain sharing. See `docs/EMBEDDED-OPERATOR-SLOTS.md` for the current architecture.
