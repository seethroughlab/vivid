# Phase 4 — UI And Interaction Audit

## Scope Reviewed

- node graph editing
- inspector system
- overlays and choosers
- session/variation workflows
- screenshot-smoke and capture paths
- post-switch layout/readability/interaction resilience

## Evidence Gathered

- Current repo state during Phase 4:
  - branch: `master`
  - worktree: dirty only from current audit files and the already-triaged screenshot-smoke follow-up docs/tests
- UI inventory:
  - `ctest --test-dir build -N | rg "test_(ui_overlay_interactions|ui_editor_interactions|ui_widget_interactions|ui_screenshot_smoke|ui_screenshot_smoke_env|ui_screenshot_smoke_harness|ui_arch_guard|capture_coordinator|control_server|graph_snapshot_contract)"`
  - discovered `10` targeted Phase 4 lanes:
    - `test_control_server`
    - `test_graph_snapshot_contract`
    - `test_ui_overlay_interactions`
    - `test_ui_editor_interactions`
    - `test_ui_widget_interactions`
    - `test_ui_screenshot_smoke`
    - `test_ui_screenshot_smoke_env`
    - `test_ui_screenshot_smoke_harness`
    - `test_ui_arch_guard`
    - `test_capture_coordinator`
- Focused Phase 4 bundle:
  - `ctest --test-dir build -R "test_ui_overlay_interactions|test_ui_editor_interactions|test_ui_widget_interactions|test_ui_screenshot_smoke|test_ui_screenshot_smoke_harness|test_ui_arch_guard|test_capture_coordinator|test_control_server|test_graph_snapshot_contract" --output-on-failure"`
  - result: `10/10` passed
  - note: the regex also matched `test_ui_screenshot_smoke_env`, so the package-aware GUI lane was exercised and passed in the focused bundle on this machine
- High-signal UI rerun:
  - `ctest --test-dir build -R "test_ui_editor_interactions|test_ui_overlay_interactions|test_ui_widget_interactions|test_ui_screenshot_smoke|test_ui_arch_guard" --output-on-failure"`
  - result: `7/7` passed
  - note: this rerun also re-exercised `test_ui_screenshot_smoke_env` and `test_ui_screenshot_smoke_harness` because of the shared `test_ui_screenshot_smoke` prefix
- Direct contract evidence from current docs and implementation:
  - [INTERFACE.md](/Users/jeff/Developer/vivid/docs/INTERFACE.md) still defines the retained-mode UI model, node graph as the central workspace, inspector-first editing, and always-on thumbnail direction
  - [UI-SCREENSHOT-SMOKE.md](/Users/jeff/Developer/vivid/docs/testing/UI-SCREENSHOT-SMOKE.md) confirms that `GUI_SMOKE` is semantic-first windowed evidence, screenshots are secondary, and `GUI_ENV` is the package/environment-sensitive companion lane
  - [control_server.md](/Users/jeff/Developer/vivid/docs/runtime/control_server.md) confirms `capture_interface` is the live-session whole-window capture path and `set_node_layout` remains the runtime-facing UI layout persistence seam
  - [MANUAL-TEST-CATALOG.md](/Users/jeff/Developer/vivid/docs/testing/MANUAL-TEST-CATALOG.md) still reserves fullscreen, external display, theme switching, and other hardware/platform flows for later manual release validation
  - [test_ui_editor_interactions.cpp](/Users/jeff/Developer/vivid/tests/test_ui_editor_interactions.cpp) covers selection/delete, drag/group drag, copy/paste, undo/redo, and layout writes
  - [test_ui_overlay_interactions.cpp](/Users/jeff/Developer/vivid/tests/test_ui_overlay_interactions.cpp) covers example browser, package browser, and graph meta editor flows
  - [test_ui_widget_interactions.cpp](/Users/jeff/Developer/vivid/tests/test_ui_widget_interactions.cpp) covers sliders, XY pads, color editing, typed value entry, dropdowns, and toggles
  - [test_ui_arch_guard.cpp](/Users/jeff/Developer/vivid/tests/test_ui_arch_guard.cpp) keeps the UI layer from reaching into `runtime/package_catalog.h`
- Historical boundary:
  - this phase uses current command evidence and current UI/runtime contracts only
  - the previous role-binding-era audit is context, not proof

## Findings

### 1. Retained editor interaction health is strong

- Classification: `pass`
- Current read:
  - `test_ui_editor_interactions` passed in both the focused bundle and the high-signal rerun
  - the current editor seams still cover delete, drag/group drag, copy/paste, undo/redo, wire reconnect, variation movement, and layout writes
- Why it matters:
  - this is the core evidence that the shipped graph editor remains trustworthy after the architecture reset and the recent UI smoke changes

### 2. Inspector and widget behavior is strong

- Classification: `pass`
- Current read:
  - `test_ui_widget_interactions` passed in both runs
  - the current widget surface still covers sliders, XY pads, color popup/hex/RGB entry, value text fields, dropdowns, and toggles
- Why it matters:
  - the current product direction depends on inspector-first editing, so Phase 4 needs clear evidence that parameter editing remains predictable and readable

### 3. Overlay and chooser workflows are healthy

- Classification: `pass`
- Current read:
  - `test_ui_overlay_interactions` passed in both runs
  - example browser, package browser callbacks/refresh, and graph meta editor flows remain green in deterministic retained-mode tests
- Why it matters:
  - these are the main non-node-graph UI surfaces users rely on during discovery, package browsing, and graph metadata workflows

### 4. Windowed screenshot-smoke integration is healthy

- Classification: `pass`
- Current read:
  - `test_ui_screenshot_smoke`, `test_ui_screenshot_smoke_harness`, and `test_ui_screenshot_smoke_env` all passed in the focused Phase 4 bundle
  - the high-signal rerun also re-exercised the same smoke family and remained green
  - the current smoke contract remains semantic-first, with screenshot baselines treated as secondary evidence after semantic assertions pass
- Why it matters:
  - Phase 4 needs real-window evidence beyond retained-mode tests, and the current GUI smoke lanes now provide that without reintroducing the old role-binding-era UI assumptions

### 5. Runtime-facing capture and UI-state seams remain aligned with the current model

- Classification: `pass`
- Current read:
  - `test_capture_coordinator`, `test_control_server`, and `test_graph_snapshot_contract` all passed in the focused bundle
  - the runtime docs still match the active capture/UI seams: `capture_interface` for full composed window capture, `set_node_layout` for persisted UI position, and graph snapshots as the source of truth rather than a separate UI-owned model
- Why it matters:
  - Phase 4 is not just about drawing widgets; it also needs confidence that the live runtime, capture path, and UI-state seams remain coherent and inspectable

### 6. UI layering guardrail remains enforced

- Classification: `pass`
- Current read:
  - `test_ui_arch_guard` passed in both runs
  - the guard still blocks direct UI dependence on `runtime/package_catalog.h`
- Why it matters:
  - after the recent simplification work, this is useful evidence that the UI has not quietly started taking on new runtime coupling again

## Required Fixes For Release

- None established by Phase 4.

## Deferred Follow-Ups

- Fullscreen, external display, theme switching, and other manual-only UI/platform flows remain part of later manual release validation rather than a current automated Phase 4 defect.

## Signoff Status

- `pass`
