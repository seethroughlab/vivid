# Phase 0 — Post-Switch Audit Baseline And Triage Setup

## Scope Reviewed

Phase 0 establishes the baseline for the current audit only. It does not attempt subsystem signoff.

This pass answers:

- what codebase is now under audit
- what the current architecture assumptions are
- what is already known vs newly discovered
- what counts as a release blocker for the current audit
- where later findings should be recorded

Primary current-architecture references:

- `docs/EMBEDDED-OPERATOR-SLOTS.md`
- `docs/SIMPLIFICATION-AND-CONSOLIDATION.md`
- `docs/ARCHITECTURE-GUARDRAILS.md`
- `docs/runtime/*.md`
- `docs/release/*.md`
- `docs/testing/*.md`

## Evidence Gathered

### Current repo state at audit start

- branch: `master`
- worktree: clean (`git status --short` returned no output)

### Architecture assumptions at audit start

The codebase under audit now assumes:

- no role bindings
- owned embedded composition for host-local behavior
- ordinary ports for graph transport
- explicit outputs for graph-visible sharing

### Automated evidence bundle

Commands run for the Phase 0 baseline:

- `git branch --show-current`
  - observed: `master`
- `git status --short`
  - observed: clean worktree at audit start
- `ctest --test-dir build -N`
  - observed: `86` tests discovered
- `ctest --test-dir build --output-on-failure`
  - observed: broad baseline exposed the same GUI screenshot-smoke failures later isolated by focused reruns
- `ctest --test-dir build -E "test_ui_screenshot_smoke|test_ui_screenshot_smoke_env|test_ui_screenshot_smoke_harness" --output-on-failure`
  - observed: `83/83` passed
- `ctest --test-dir build -R "test_app_update_manager|test_package_update_logic|test_control_server|test_export_pipeline|test_demo_graphs|test_media_headless|test_capture_coordinator" --output-on-failure`
  - observed: `7/7` passed
- `ctest --test-dir build -R "test_ui_screenshot_smoke" --output-on-failure`
  - observed: `1/3` passed, `2/3` failed
  - failing tests: `test_ui_screenshot_smoke`, `test_ui_screenshot_smoke_env`
- follow-up screenshot-smoke triage rerun after fixing obsolete expectations and GUI-env package-path setup:
  - `ctest --test-dir build -R "test_ui_screenshot_smoke|test_ui_screenshot_smoke_env|test_ui_screenshot_smoke_harness" --output-on-failure`
  - observed: `test_ui_screenshot_smoke` passed
  - observed: `test_ui_screenshot_smoke_env` passed
  - observed: `test_ui_screenshot_smoke_harness` passed
- `./build/test_demo_graphs ./build/graphs`
  - observed: `19` passed, `0` failed, `65` skipped
  - environment note: the direct smoke run reported no GPU available, so GPU-only graphs were skipped in this headless lane

### Notable outcomes from the current evidence

- The broad non-screenshot automated suite is currently healthy on this machine.
- The highest-signal release-facing automated lanes are currently healthy:
  - `test_app_update_manager`
  - `test_package_update_logic`
  - `test_control_server`
  - `test_export_pipeline`
  - `test_demo_graphs`
  - `test_media_headless`
  - `test_capture_coordinator`
- The original Phase 0 baseline found a concentrated GUI screenshot-smoke failure cluster.
- A same-branch follow-up triage pass resolved that cluster by:
  - removing the obsolete `scale_lfo` smoke case after the move to owned embedded composition
  - updating the graph-drop reload expectations to match the current `instanced_shapes_demo.json` graph
  - fixing GUI-env package-path propagation so sibling package operators resolve in the spawned smoke session
- Current read after that triage:
  - the GUI screenshot-smoke lanes are green on this machine
  - the Phase 4 audit should still review UI behavior directly, but screenshot smoke is no longer an active deferred issue from Phase 0

### Release and manual-validation references reviewed

- `docs/release/RELEASE-CHECKLIST.md`
- `docs/testing/README.md`
- `docs/testing/MANUAL-TEST-CATALOG.md`

These references establish that the current baseline should treat:

- release-facing automated lanes as high-signal preflight evidence
- GUI screenshot smoke as optional but valuable for UI-sensitive release validation
- manual validation as still necessary for graph editing, save/load, audio routing, GPU rendering, packages, capture, MIDI, fullscreen/display behavior, and other not-fully-automated user workflows

### Historical boundary

- the previous audit of the earlier role-binding-era architecture now lives in:
  - `docs/audit-history/role-binding-era/`
- that older audit can inform what to inspect, but it is not release evidence for this audit

## Known Issues At Audit Start

### 1. Headless demo smoke is only a partial release signal on this machine

- Classification: `known at audit start`
- Current read:
  - `./build/test_demo_graphs ./build/graphs` passed with `19` passing demo graphs and `65` skipped because the machine reported no GPU in that lane
  - this is still useful smoke coverage, but it does not replace GPU-available validation for release confidence

### 2. Manual release validation is still outstanding for the current architecture

- Classification: `required before release`
- Current read:
  - the automated baseline is strong enough to start subsystem audit phases
  - it does not replace the manual flows called out in `docs/release/RELEASE-CHECKLIST.md` and `docs/testing/MANUAL-TEST-CATALOG.md`
  - save/load, inspector behavior, audio output, GPU rendering, package lifecycle, capture, and fullscreen/display workflows still need fresh release-era validation under the embedded-composition model

## Current Verification Surface

### Trusted automated surfaces

- `ctest --test-dir build -E "test_ui_screenshot_smoke|test_ui_screenshot_smoke_env|test_ui_screenshot_smoke_harness" --output-on-failure`
  - result: `83/83` passed
- `ctest --test-dir build -R "test_app_update_manager|test_package_update_logic|test_control_server|test_export_pipeline|test_demo_graphs|test_media_headless|test_capture_coordinator" --output-on-failure`
  - result: `7/7` passed
- `./build/test_demo_graphs ./build/graphs`
  - result: `19` passed, `0` failed, `65` skipped

### Weak or environment-sensitive surfaces

- headless demo smoke on this machine skips GPU-only graphs when no GPU is available
- GPU-available demo validation is still needed for full release confidence
- manual release validation remains necessary for save/load, rendering, audio, capture, display, and device-sensitive workflows

### Manual smoke flows that still matter

- graph editing:
  - add/delete/connect/disconnect/copy/paste
- parameter UI:
  - sliders, dropdowns, toggles, typed input, color controls
- file I/O:
  - save/load, recent files, Finder/open-file association
- audio and GPU:
  - audible output, routing, rendering, texture flow, shader error handling
- packages and ecosystem:
  - install/uninstall, palette refresh, package artifact cleanup
- platform-specific flows:
  - MIDI hot-plug, capture, theme switching, fullscreen, external display behavior

## Release Blocker Rubric

Treat any of the following as a release blocker unless explicitly reclassified:

1. crash, hang, data loss, or graph corruption
2. broken save/load/reload or broken host-local state restoration
3. broken package/operator loading or rebuild flow in core workflows
4. broken export or release/update path
5. major unusable UI workflow
6. graph truth or host-local composition behaving differently from the current architectural contract

Non-blocking issues can still be tagged as:

- `required before release`
- `deferred`

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
- embedded-slot / host-state correctness
- signal-port discipline
- snapshot consistency

### Phase 3 — Domain Pipelines And Cross-Domain Behavior

- control/audio/GPU/media behavior
- domain bridges
- timing-sensitive and analysis-sensitive workflows
- explicit-output correctness across domains

### Phase 4 — UI And Interaction Audit

- node graph editing
- inspector system
- overlays and choosers
- session/variation workflows
- post-switch layout/readability/interaction resilience

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

## Signoff Status

- `pass with defer`

Phase 0 established a real baseline for the post-switch codebase:

- the repo state at audit start was clean on `master`
- the broad non-screenshot automated suite passed
- the highest-signal release-facing automated lanes passed
- the initial screenshot-smoke failures were triaged and cleared on the same branch

Carry forward:

- Phase 1 should start from the assumption that runtime core and release-facing automated preflight are broadly healthy
- Phase 4 should still validate the post-switch UI directly, but it no longer needs to inherit screenshot-smoke breakage as an open issue
