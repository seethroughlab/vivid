# Vivid Code Audit Tracker

## Purpose

This document tracks the live whole-project audit after the exploration phases.
It is separate from the code-review preparation notes:

- `docs/CODE_REVIEW.md` explains how to explore the codebase.
- `docs/internal/CODE-REVIEW-PHASE*.md` capture the orientation passes.
- This file tracks the actual audit sequence, findings status, and what is next.

The goal is to keep one current view of:

- which audit slices are complete
- which findings were identified
- which findings were fixed
- which runtime or product boundaries still need review

## Audit Order

The current audit sequence is:

1. Runtime consistency boundary
2. Operator contract and loader behavior
3. Control server and package workflow boundary
4. UI/runtime seam
5. Export, capture, settings, and release/runtime-update paths
6. Sibling-package / ecosystem audit

Within the runtime consistency boundary, the current sub-order is:

1. runtime rebuild and save/reload identity
2. `RuntimeAPI` rebuild/persistence semantics
3. `AudioEngine` cross-domain snapshot assumptions
4. hot-reload consistency between `Scheduler` and `AudioEngine`
5. snapshot/undo-redo transactional restore behavior

## Status

### Completed

- Exploration Phases 1-7
- Runtime consistency pass 1
- Runtime consistency pass 2
- Runtime consistency pass 3
- Runtime consistency pass 4
- Operator contract and loader pass 1
- Control server and package workflow boundary
- UI/runtime seam
- Export, capture, settings, and release/runtime-update paths
- Sibling-package / ecosystem audit

### In Progress

- None

### Not Started

- None

## Completed Audit Work

### Runtime Consistency Pass 1

Reviewed:

- `src/runtime/main.cpp`
- `src/runtime/runtime_api.cpp`
- `src/runtime/audio_engine.cpp`
- `src/runtime/scheduler.cpp`
- `src/runtime/graph.cpp`

Findings identified:

1. Package-refresh rebuild bypassed coherent audio-engine rebuild, leaving stale audio mappings possible.
2. `RuntimeAPI::save_as()` did not retarget the live graph path/source identity.

Fixes landed:

- Rebuilt live runtime coherently after package refresh in `main.cpp`
- Added source-path setter on `Graph`
- Retargeted runtime graph/source identity in `RuntimeAPI::save_as()`
- Added runtime regression coverage for the save-as path

Files changed during this pass:

- `src/runtime/graph.h`
- `src/runtime/main.cpp`
- `src/runtime/runtime_api.cpp`
- `tests/test_runtime_api.cpp`

Validation:

- `cmake --build build --target test_runtime_api`
- `ctest --test-dir build --output-on-failure -R "test_runtime_api|test_control_server"`

### Runtime Consistency Pass 2

Reviewed:

- deeper `RuntimeAPI::reload()` / persistence semantics
- deeper `AudioEngine` snapshot and callback input assumptions

Findings identified:

1. Failed `reload()` could tear down runtime state and leave the graph/source identity in a broken state.
2. Control-to-audio `FLOAT` inputs were not using the same snapshot contract as other cross-domain audio inputs.

Fixes landed:

- Made `RuntimeAPI::reload()` restore the previous graph/runtime state on load or rebuild failure
- Added `FLOAT` input values to the audio snapshot buffer
- Moved control-to-audio `FLOAT` propagation onto the double-buffered snapshot path
- Added regression coverage for reload failure restore
- Added regression coverage for the audio `FLOAT` snapshot contract

Files changed during this pass:

- `src/runtime/runtime_api.cpp`
- `src/runtime/audio_engine.h`
- `src/runtime/audio_engine.cpp`
- `tests/test_runtime_api.cpp`
- `tests/test_audio_float_snapshot.cpp`
- `tests/operators/audio_float_cv_op.cpp`
- `CMakeLists.txt`

Validation:

- `ctest --test-dir build --output-on-failure -R "test_runtime_api|test_audio_float_snapshot"`

### Runtime Consistency Pass 3

Reviewed:

- hot-reload orchestration in `src/runtime/main.cpp`
- loader swap behavior in `src/runtime/operator_loader.cpp`
- registry reload path in `src/runtime/operator_registry.cpp`
- scheduler/audio reload behavior in `src/runtime/scheduler.cpp` and `src/runtime/audio_engine.cpp`

Findings identified:

1. Loader swap was not truly atomic when custom port type registration failed.
2. Main-loop hot reload treated scheduler success as global success, even if audio reload failed.
3. Descriptor-shape changes were not guarded, even though cached wire/mapping metadata is not rebuilt on hot reload.

Fixes landed:

- Moved custom port type registration ahead of loader swap commit
- Preserved the previous loader on registration failure
- Added descriptor compatibility guard for hot reload:
  - params must preserve existing prefix layout
  - port layout must remain unchanged
- Tightened hot-reload success reporting so audio reload failure is not reported as full success
- Added regression coverage for incompatible port-layout rejection

Files changed during this pass:

- `src/runtime/operator_loader.cpp`
- `src/runtime/main.cpp`
- `tests/test_hot_reload.cpp`
- `tests/operators/test_op_incompatible_port.cpp`
- `CMakeLists.txt`

Validation:

- `ctest --test-dir build --output-on-failure -R "test_hot_reload|test_operator_loader|test_runtime_api|test_audio_float_snapshot"`

### Runtime Consistency Pass 4

Reviewed:

- `src/runtime/runtime_api.cpp`
- `src/runtime/control_server.cpp`
- `src/runtime/undo_manager.cpp`
- existing snapshot / undo regression tests

Findings identified:

- No concrete runtime-consistency bugs found in the snapshot / undo transactionality path.

Hardening landed:

- Added regression coverage for malformed `apply_snapshot_json()` rollback
- Added regression coverage for failed snapshot-apply dirty-state preservation
- Added regression coverage for `source_path` identity on saved vs unsaved snapshot apply

Files changed during this pass:

- `tests/test_runtime_api.cpp`

Validation:

- `ctest --test-dir build --output-on-failure -R "test_runtime_api"`

## Hardening Follow-Through

### Phase 1: Lock In The New Runtime Guarantees

Status:

- complete

Covered by:

- `test_runtime_api`
- `test_hot_reload`
- `test_audio_hot_reload`
- `test_export_pipeline`
- `test_capture_coordinator`
- `test_app_update_manager`
- `test_control_server`

### Phase 2: Finish Package-Test Contract Hardening

Status:

- complete

What landed:

1. The manifest package-test contract is now explicit and intentionally hybrid:
   - `tests.graphs` for graph smoke / graph contract coverage
   - manifest `tests.cpp` for lightweight self-contained single-source entrypoints
   - package-local CMake / CTest remains canonical for heavier package-specific C++ tests
2. `PackageTestRunner` now performs deterministic early validation and stable classification for:
   - `missing_test_file`
   - `unsupported_test_extension`
   - `path_outside_package`
   - `duplicate_test_entry`
   - `unsupported_cpp_test_shape`
   - graph skip/failure codes such as `graph_needs_gpu` and `graph_needs_audio`
3. `PackageCompiler::compile_test(...)` now normalizes paths, rejects escaping paths, and preserves deterministic compile failure reporting.
4. `test_package` control-server output now exposes:
   - per-test `code`
   - package-level `notes`
5. Core now has a representative ecosystem-level package-contract test:
   - `test_package_contract_ecosystem`

Files changed during this phase:

- `src/runtime/package_test_runner.h`
- `src/runtime/package_test_runner.cpp`
- `src/runtime/package_compiler.h`
- `src/runtime/package_compiler.cpp`
- `src/runtime/control_server.cpp`
- `tests/test_package_test_runner.cpp`
- `tests/test_package_contract_ecosystem.cpp`
- `docs/runtime/package_system.md`
- `docs/runtime/control_server.md`
- `docs/internal/PACKAGE-TEST-CONTRACT.md`
- `CMakeLists.txt`

Validation:

- `ctest --test-dir build --output-on-failure -R "test_package_test_runner|test_package_contract_ecosystem|test_control_server"`

### Phase 3: Tighten UI / Runtime Contract Surfaces

Status:

- complete

Current focus:

1. Formalize `GraphSnapshot` as an explicit UI read-model contract.
2. Keep package-browser data snapshot-backed and refresh-safe.
3. Add regression coverage for popup text-edit field behavior and broken-connection visibility.

Hardening landed so far:

- Added `GraphSnapshot` helper semantics for broken connections:
  - `ConnectionSnapshot::is_broken()`
  - `GraphSnapshot::find_connection(...)`
  - `GraphSnapshot::broken_connection_count()`
  - `GraphSnapshot::has_broken_connections()`
- Added explicit package-browser refresh helper in `NodeGraphUI`
- Fixed package-browser refresh to update on metadata changes even when entry count is unchanged
- Added UI regressions for:
  - package-browser same-count metadata refresh
  - graph-meta editor text-edit field focus and save behavior
  - broken-connection contract helpers
- Added result-aware UI command helpers for critical multi-step mutations:
  - `try_connect(...)`
  - `try_disconnect(...)`
- Hardened chooser insert-on-wire splicing so the original wire remains intact
  unless both replacement connects succeed and the original disconnect succeeds
- Added chooser splice rollback regression coverage for failed replacement wiring

Files changed during this phase so far:

- `src/ui/graph_snapshot.h`
- `src/ui/node_graph.h`
- `src/ui/node_graph.cpp`
- `src/ui/node_graph_overlays.cpp`
- `src/ui/node_graph_overlay_draw.cpp`
- `src/ui/ui_command_sink.h`
- `src/runtime/runtime_command_sink.h`
- `tests/test_ui_overlay_interactions.cpp`
- `tests/test_graph_snapshot_contract.cpp`
- `docs/internal/GRAPH-SNAPSHOT-CONTRACT.md`
- `CMakeLists.txt`

Validation:

- `ctest --test-dir build --output-on-failure -R "test_graph_snapshot_contract|test_ui_overlay_interactions|test_control_server"`

### Control Server And Package Workflow Audit

Reviewed:

- `src/runtime/control_server.cpp`
- `src/runtime/package_manager.cpp`
- `src/runtime/package_compiler.cpp`
- `src/runtime/package_catalog.cpp`
- `src/runtime/package_test_runner.cpp`
- related tests and package fixture coverage

Findings identified:

1. `uninstall_package` / `unlink_package` could unload live package code while the active graph still referenced it.
2. `rebuild_package` only handled scheduler-side instances and could leave audio/runtime state stale.
3. Package mutation endpoints could report success even when the follow-up runtime refresh failed.

Fixes landed:

- Routed live package mutation recovery through transactional snapshot rebuild instead of mutating live scheduler state in place
- Made rebuild/unlink flows runtime-safe for active graphs
- Surfaced refresh failure explicitly instead of silently reporting success
- Added control-server regression coverage for live linked package add/rebuild/unlink flows

Files changed during this pass:

- `src/runtime/control_server.cpp`
- `tests/test_control_server.cpp`

Validation:

- `cmake --build build --target test_control_server`
- `ctest --test-dir build --output-on-failure -R "test_control_server|test_runtime_api"`

### UI / Runtime Seam Audit

Reviewed:

- `src/ui/node_graph.cpp`
- `src/ui/node_graph_draw.cpp`
- `src/ui/node_graph_input.cpp`
- `src/ui/node_graph_overlays.cpp`
- `src/ui/node_graph_overlay_draw.cpp`
- `src/ui/node_graph_overlay_input.cpp`
- `src/ui/ui_command_sink.h`
- `src/runtime/main.cpp`
- `src/runtime/runtime_command_sink.h`
- `src/ui/graph_snapshot.h`

Findings identified:

1. The package browser crossed the UI/runtime seam unsafely by reading live package-manager state on the UI thread while background package actions could mutate it.
2. `build_graph_snapshot()` silently dropped graph connections whose endpoints no longer resolved, so the editor could hide real but invalid wires.
3. The `UICommandSink` mutation contract was write-only, and chooser-driven insert/connect flows assumed node creation succeeded before disconnecting or rewiring.

Fixes landed:

- Made the package browser list snapshot-backed in `main.cpp`, with cache refreshes only on the main thread at safe points
- Preserved invalid/stale graph connections in `GraphSnapshot` with explicit broken-wire metadata
- Added broken-wire rendering/tooltip/inspector treatment in `NodeGraphUI`
- Added `try_add_node(...)` to the UI command boundary and used it to gate chooser insert/connect rewiring on successful node creation

Files changed during this pass:

- `src/runtime/main.cpp`
- `src/runtime/runtime_command_sink.h`
- `src/ui/graph_snapshot.h`
- `src/ui/node_graph.cpp`
- `src/ui/node_graph_draw.cpp`
- `src/ui/ui_command_sink.h`

Validation:

- `cmake --build build --target test_control_server test_runtime_api test_hot_reload test_ui_overlay_interactions`
- `./build/test_control_server ./build`
- `./build/test_ui_overlay_interactions`
- `ctest --test-dir build --output-on-failure -R "test_ui_overlay_interactions|test_runtime_api|test_hot_reload"`

Notes:

- `ctest` invocation of `test_control_server` still has a working-directory-dependent scaffold fallback failure. The direct binary invocation with the expected build-root argument passed after the UI/runtime seam fixes, so that `ctest` quirk is tracked separately from this audit slice.

## Next Audit Slice

### Export / Capture / Settings / Release-Path Audit

Scope:

- `src/export/export_pipeline.cpp`
- `src/runtime/capture_coordinator.cpp`
- `src/runtime/settings.cpp`
- `src/runtime/app_update_manager.cpp`
- release/update/save-path interactions from `src/runtime/main.cpp`

Questions to answer:

1. Do export/capture flows consume the same runtime state coherently as the interactive app?
2. Are settings persistence and startup/bootstrap paths consistent with current runtime behavior?
3. Do update/release-path surfaces preserve non-destructive behavior and clear failure reporting?
4. Are there hidden filesystem or lifecycle assumptions that differ between interactive, headless, and exported runs?

Desired output:

- ordered findings, if any
- recommended fix order if lifecycle, persistence, or export/capture contracts drift

Current findings identified:

1. Standalone export copies the built binary to `output_name` in the current working directory instead of the user-selected destination path from the UI export flow.
2. Standalone export registers static operators but does not register their custom port types, so exported builds can fail for graphs that rely on custom-type contracts such as `media_stream_v1`.
3. Capture stop-reporting always returns `{"ok":true}` even if `AVExporter::finish()` timed out or failed.
4. `AppUpdateManager` uses a detached fetch thread with a raw `this` pointer and no lifetime coordination, which makes shutdown during an in-flight update check unsafe.

Fixes landed:

1. Standalone export now emits and registers the required `VividPortTypeInfo` records for the custom port types used by exported operators before registering the static operators.
2. Export now respects the final output path supplied by the caller instead of copying the binary into the current working directory.
3. Capture stop-reporting now returns `ok:false` when exporter finalization fails and includes the output path for diagnostics.
4. `AppUpdateManager` now owns a joinable worker thread and joins it on destruction instead of detaching a raw-`this` background thread.

Files changed during this pass:

- `src/export/export_pipeline.h`
- `src/export/export_pipeline.cpp`
- `src/runtime/main.cpp`
- `src/runtime/capture_coordinator.cpp`
- `src/runtime/app_update_manager.h`
- `src/runtime/app_update_manager.cpp`

Validation:

- `cmake --build build --target vivid test_capture_coordinator test_app_update_manager test_settings test_control_server`
- `ctest --test-dir build --output-on-failure -R "test_capture_coordinator|test_app_update_manager|test_settings|test_runtime_api|test_ui_overlay_interactions|test_hot_reload"`

## Post-Audit Hardening Progress

### Phase 1 Workstream A: Runtime Rollback And Refresh Regressions

Status:

- in progress

What landed:

- Confirmed the previously added rollback regressions in:
  - `tests/test_runtime_api.cpp`
  - `tests/test_control_server.cpp`
- Added explicit audio-side hot-reload safety coverage so rejected reloads are protected across domains, not just in the scheduler:
  - `tests/operators/audio_reload_v1.cpp`
  - `tests/operators/audio_reload_v2.cpp`
  - `tests/operators/audio_reload_incompatible.cpp`
  - `tests/test_audio_hot_reload.cpp`
- Added build wiring for the new audio hot-reload regression:
  - `CMakeLists.txt`

What this covers now:

1. compatible audio hot reload preserves existing params and applies new defaults
2. descriptor-incompatible audio reload is rejected safely
3. after rejected reload, the previous audio operator remains active and usable

Validation:

- `cmake --build build --target test_audio_hot_reload`
- `ctest --test-dir build --output-on-failure -R "test_audio_hot_reload|test_hot_reload|test_runtime_api"`

Open note:

- `ctest --test-dir build --output-on-failure -R "test_control_server"` still hits the older scaffold fallback failure that was already noted during the audit. That issue is unrelated to the new rollback/hot-reload regression and belongs under Phase 1 Workstream D (harness and invocation cleanup), not as a blocker on the new audio hot-reload coverage itself.

### Phase 1 Workstream B: Export Path Regression Coverage

Status:

- completed

What landed:

- Added a minimal custom-port export fixture operator:
  - `tests/operators/export_custom_port_op.cpp`
- Added direct export-pipeline regression coverage:
  - `tests/test_export_pipeline.cpp`
- Added build wiring for the new export regression:
  - `CMakeLists.txt`

What this covers now:

1. `resolve_operators()` records required custom port types for exported operators
2. `generate_static_registry()` emits `vivid_register_port_type(...)` entries for required custom types
3. generated static registry preserves the stable custom type id string
4. `copy_output()` copies the standalone binary to the selected destination path
5. WebGPU sidecar dylibs are copied next to that selected destination path

Validation:

- `cmake --build build --target test_export_pipeline`
- `ctest --test-dir build --output-on-failure -R "test_export_pipeline|test_audio_hot_reload|test_hot_reload|test_runtime_api"`

### Phase 1 Workstream C: Capture And Update Lifecycle Regressions

Status:

- completed

What landed:

- Added a small exporter injection seam for capture tests:
  - `src/runtime/av_exporter.h`
  - `src/runtime/capture_coordinator.h`
  - `src/runtime/capture_coordinator.cpp`
- Added focused capture failure coverage:
  - `tests/test_capture_coordinator.cpp`
- Added app-update worker lifetime/concurrency test hooks:
  - `src/runtime/app_update_manager.h`
  - `src/runtime/app_update_manager.cpp`
- Extended app-update regression coverage:
  - `tests/test_app_update_manager.cpp`

What this covers now:

1. stop-recording returns `ok:false` when exporter finalization fails
2. stop-recording failure responses still include the recording path
3. repeated `refresh()` calls do not create overlapping app-update fetch workers
4. destroying `AppUpdateManager` after `refresh()` leaves no active worker behind

Validation:

- `cmake --build build --target test_capture_coordinator test_app_update_manager`
- `ctest --test-dir build --output-on-failure -R "test_capture_coordinator|test_app_update_manager|test_export_pipeline|test_audio_hot_reload|test_runtime_api"`

### Phase 1 Workstream D: Harness And Invocation Cleanup

Status:

- completed

What landed:

- fixed the `test_control_server` CTest invocation to pass the expected build directory argument:
  - `CMakeLists.txt`
- isolated `test_control_server` from unrelated `build/packages/*` fixture directories by moving the test into its own temporary cwd before package discovery:
  - `tests/test_control_server.cpp`

What this covers now:

1. `test_control_server` resolves its fixture/build paths consistently under both direct invocation and `ctest`
2. package auto-destination tests no longer pick up unrelated local fixture packages from the shared build directory

Validation:

- `ctest --test-dir build --output-on-failure -R "test_control_server"`

### Phase 1 Hardening Status

Status:

- completed

Phase 1 gate validation:

- `ctest --test-dir build --output-on-failure -R "test_runtime_api|test_control_server|test_hot_reload|test_audio_hot_reload|test_export_pipeline|test_capture_coordinator|test_app_update_manager"`

Outcome:

- all targeted Phase 1 hardening regressions now pass under normal `ctest` execution

Fixes landed:

- Added live-graph snapshot capture before destructive package mutations.
- `uninstall_package` and `unlink_package` now rebuild the live runtime through `apply_snapshot_json(...)` after package mutation.
- `rebuild_package` now uses the same transactional snapshot-restore path instead of the old scheduler-only instance swap logic.
- `install_package` and `link_package` now surface runtime refresh failure explicitly if missing-operator auto-reload fails.
- Added control-server regression coverage for:
  - linked package discovery under the test package root
  - live linked package add/rebuild/unlink flow
  - rebuilt operator output actually changing after package rebuild
  - unlink degrading an in-use node to a missing-operator placeholder instead of leaving stale code live
- Hardened the control-server test fixture so package state is reset between runs and package compile paths resolve the repo source root consistently under both direct and CTest execution.

Files changed during this pass:

- `src/runtime/control_server.cpp`
- `tests/test_control_server.cpp`

Validation:

- `cmake --build build --target test_control_server`
- `ctest --test-dir build --output-on-failure -R "test_control_server|test_runtime_api"`

### Operator Contract And Loader Pass 1

Reviewed:

- `src/operator_api/operator.h`
- `src/operator_api/types.h`
- `src/operator_api/type_id.h`
- `src/operator_api/create_request.h`
- `src/runtime/operator_loader.cpp`
- `src/runtime/operator_registry.cpp`
- `src/runtime/operator_creator.cpp`

Findings identified:

1. Hot reload could still commit a broken loader if `vivid_descriptor()` returned null or an unnamed descriptor.
2. Deferred probing did not preserve the full operator descriptor contract (`semantic_tag`, `has_process_audio`, `has_process_gpu`).
3. Operator scaffolding accepted zero-sized custom ports and silently generated a different contract than requested.

Fixes landed:

- Hardened `OperatorLoader::load()` to reject null or unnamed descriptors before swap commit
- Extended deferred descriptor copying to preserve port semantic tags and process-domain flags
- Tightened `OperatorCreator` validation so custom ports must declare `payload_size > 0`
- Added a null-descriptor plugin regression
- Added deferred-probe metadata fidelity regression coverage
- Added zero-size custom port creator validation coverage

Files changed during this pass:

- `src/runtime/operator_loader.cpp`
- `src/runtime/operator_registry.h`
- `src/runtime/operator_registry.cpp`
- `src/runtime/operator_creator.cpp`
- `tests/operators/audio_test_op.cpp`
- `tests/operators/test_op_null_desc.cpp`
- `tests/test_operator_loader.cpp`
- `tests/test_operator_creator.cpp`
- `CMakeLists.txt`

Validation:

- `ctest --test-dir build --output-on-failure -R "test_operator_loader|test_operator_creator|test_hot_reload"`

### Sibling-Package / Ecosystem Audit

Reviewed:

- package-facing contract baseline in:
  - `src/runtime/package_manager.cpp`
  - `src/runtime/package_test_runner.cpp`
  - `src/operator_api/types.h`
  - `src/operator_api/port_type_registry.h`
- sibling repos:
  - `../vivid-cef`
  - `../vivid-3d`
  - `../vivid-drums`
  - `../vivid-sequencers`
  - `../vivid-glitch`
  - `../vivid-wavetable`

Cross-repo ecosystem findings identified:

1. The main ecosystem contract drift is test ownership metadata: five of the six package manifests either omit the `tests` block entirely or declare empty `tests.graphs` / `tests.cpp` arrays even though the repos contain real package tests and graph smoke assets. That means Vivid's package-test surface (`run_package_tests`) cannot execute the same package coverage that package-local CI is already relying on.
2. Package-local smoke workflows often bypass package-owned tests and rely on bespoke shell steps instead of the package manifest contract, so CI coverage shape and runtime/package-manager expectations have drifted apart.

Fixes landed:

1. Added manifest `tests` metadata across all six audited package repos so package-owned graph smoke assets and lightweight standalone C++ tests are now declared explicitly.
2. Updated package smoke workflows in `vivid-3d`, `vivid-drums`, `vivid-sequencers`, `vivid-glitch`, and `vivid-wavetable` so CI now builds and runs the substantive package-owned tests instead of only manifest checks and graph smoke.
3. Fixed follow-on validation issues found while exercising those workflow changes:
   - `vivid-drums`: added a default `VIVID_PLUGIN_SUFFIX` so package-local builds emit `.dylib` plugins and the smoke copy step works
   - `vivid-wavetable`: removed the non-existent `test_wavetable_position_env` target from the smoke workflow
   - `vivid-sequencers`: updated `test_state_machine.cpp` to drive the current audio-operator API and fixed `test_pattern_algebra`'s missing `port_type_registry.cpp` link source

Residual gap:

- Package-local validation docs were aligned where the contract had clearly drifted: `vivid-cef` AGENTS and `vivid-wavetable` README/AGENTS now describe the real deterministic test and smoke workflow.
- Some heavier package tests still rely on package-local CMake/CTest wiring rather than the generic `PackageTestRunner` compile path, so manifest `cpp` entries were kept conservative where the core runner cannot yet reproduce the full local test environment.
- No blocking package-validation failures remain after the follow-up fix pass.

Repo-by-repo status:

#### `vivid-cef` — complete

Purpose / ownership summary:

- Browser integration package with one GPU operator (`browser`) and one audio operator (`browser_audio_in`), plus a helper executable and CEF framework/runtime assets.
- Highest lifecycle risk in the set because it owns plugin-loaded browser runtime state and subprocess/framework packaging.

Package contract surface:

- Manifest: `vivid-package.json`
- Build integration: package-local `CMakeLists.txt` builds operators, helper, deterministic tests, and copies CEF runtime assets
- Operator loading shape: one GPU plugin + one audio plugin
- Graphs/examples: 7 package graphs plus HTML examples
- Tests: 4 deterministic C++ tests in `tests/`

Findings identified:

1. `P1` The manifest advertises no package tests or smoke graphs even though the repo contains both deterministic C++ tests and package graph fixtures. This makes the package-manager/package-test-runner surface blind to the package's real validation set.
   Files:
   - `../vivid-cef/vivid-package.json`
   - `../vivid-cef/tests/test_browser_cef_gate.cpp`
   - `../vivid-cef/tests/test_browser_audio_bridge.cpp`
   - `../vivid-cef/graphs/browser_audio.json`
2. `P2` Package docs and AGENTS drift from the actual contract by claiming there are no automated tests and describing `tests/` as placeholder-only. That is no longer true and will mislead future maintenance of the package's validation path.
   Files:
   - `../vivid-cef/AGENTS.md`
   - `../vivid-cef/README.md`

Validation notes:

- README / AGENTS / manifest inspected
- `CMakeLists.txt` and `.github/workflows/smoke.yml` inspected
- Package-local build passed
- Deterministic package tests passed: 4/4
- Graph smoke from `vivid/build` loaded the package correctly and skipped all 7 graphs in no-GPU mode

Recommended fix order:

1. Populate the manifest `tests` block with real graph and C++ test entries
2. Bring AGENTS / README validation guidance in line with the real deterministic test suite

#### `vivid-3d` — complete

Purpose / ownership summary:

- Main 3D GPU package for Vivid, including scene-fragment custom-port contracts and the largest graph/demo surface in the sibling set.

Package contract surface:

- Manifest: `vivid-package.json`
- Build integration: package-local `CMakeLists.txt`
- Operator loading shape: GPU-only package with custom scene fragment port helpers in `include/operator_api/gpu_3d.h`
- Graphs/examples: 20 graph demos
- Tests: 16 package-owned C++ tests

Findings identified:

1. `P1` The manifest does not declare any package tests, so Vivid's package-test runner cannot execute the repo's substantial test suite or graph smoke set through the package contract.
   Files:
   - `../vivid-3d/vivid-package.json`
   - `../vivid-3d/tests/test_render_3d.cpp`
   - `../vivid-3d/tests/test_scene3d.cpp`
2. `P1` The smoke workflow builds operators and runs graph smoke, but it does not run the repo's package-owned C++ test suite. That leaves the richest GPU/custom-port package in the ecosystem under-validated relative to its own test inventory.
   Files:
   - `../vivid-3d/.github/workflows/smoke.yml`
   - `../vivid-3d/tests/test_gpu_3d.cpp`
   - `../vivid-3d/tests/test_material3d.cpp`

Validation notes:

- README / AGENTS / manifest inspected
- custom-port declaration path sampled in `include/operator_api/gpu_3d.h`
- workflow and test inventory inspected
- Package-local build passed
- Package C++ tests passed: 16/16
- Graph smoke from `vivid/build` loaded package operators correctly and skipped the graph set in no-GPU mode

Recommended fix order:

1. Add manifest `tests` entries for package graphs and core package-owned tests
2. Extend the smoke workflow to run the package C++ tests before graph smoke

#### `vivid-drums` — complete

Purpose / ownership summary:

- Drum synthesis package with six audio operators, a small graph/demo set, and factory presets.

Package contract surface:

- Manifest: `vivid-package.json`
- Build integration: package-local `CMakeLists.txt`
- Graphs/examples: 4 package graphs
- Tests: manifest test plus drum-stack asset regression

Findings identified:

1. `P1` The manifest does not declare package tests or graphs, so package-manager-driven test execution cannot see the repo's real coverage surface.
   Files:
   - `../vivid-drums/vivid-package.json`
   - `../vivid-drums/tests/test_drum_stack_assets.cpp`
   - `../vivid-drums/graphs/drum_stack_foundation.json`
2. `P2` The smoke workflow only builds and runs `test_package_manifest`; it skips `test_drum_stack_assets`, which is the package's substantive behavior/asset regression.
   Files:
   - `../vivid-drums/.github/workflows/smoke.yml`
   - `../vivid-drums/CMakeLists.txt`

Validation notes:

- README / AGENTS / manifest inspected
- workflow and tests inspected
- Package-local build passed
- Package C++ tests passed: 1/1
- Graph smoke from `vivid/build` passed 2 graphs and skipped 2 GPU-dependent graphs

Recommended fix order:

1. Declare graphs and tests in the manifest
2. Run `test_drum_stack_assets` in package CI

#### `vivid-sequencers` — complete

Purpose / ownership summary:

- Control-operator package for sequencing, pattern algebra, and state-machine style musical control logic.

Package contract surface:

- Manifest: `vivid-package.json`
- Build integration: package-local `CMakeLists.txt`
- Graphs/examples: 6 package graphs
- Tests: package manifest + arpeggiator/state-machine/pattern-algebra tests

Findings identified:

1. `P1` The manifest does not declare package tests or graph smoke entries, so the runtime package-test contract does not reflect the repo's actual validation assets.
   Files:
   - `../vivid-sequencers/vivid-package.json`
   - `../vivid-sequencers/tests/test_arpeggiator_patterns.cpp`
   - `../vivid-sequencers/tests/test_pattern_algebra.cpp`
2. `P1` The smoke workflow builds package operators and runs graph smoke, but it does not run the repo's package-owned C++ tests. That leaves logic-heavy operators validated mainly by graph smoke rather than direct behavior tests.
   Files:
   - `../vivid-sequencers/.github/workflows/smoke.yml`
   - `../vivid-sequencers/CMakeLists.txt`

Validation notes:

- README / AGENTS / manifest inspected
- workflow and test inventory inspected
- Package-local operator build passed
- Package-local C++ tests passed: 3/3 after converting `test_pattern_algebra` to exercise audio operators through the real loader/process-audio path
- Graph smoke from `vivid/build` passed 5 graphs and skipped 1 GPU-dependent graph

Recommended fix order:

1. Add manifest `tests` entries for graphs and C++ tests
2. Run the package C++ tests in CI before graph smoke

#### `vivid-glitch` — complete

Purpose / ownership summary:

- Mixed audio/GPU effects package with the broadest domain spread in the sibling set after `vivid-cef`.

Package contract surface:

- Manifest: `vivid-package.json`
- Build integration: package-local `CMakeLists.txt`
- Graphs/examples: 7 graphs
- Tests: one package-owned C++ utility regression (`test_rate_utils`)

Findings identified:

1. `P1` The manifest explicitly declares empty `tests.graphs` and `tests.cpp` arrays even though the repo contains both smoke graphs and a package-owned C++ regression. This is direct contract drift, not merely omission.
   Files:
   - `../vivid-glitch/vivid-package.json`
   - `../vivid-glitch/tests/cpp/test_rate_utils.cpp`
   - `../vivid-glitch/graphs/glitch_demo.json`
2. `P2` The smoke workflow builds operators and runs graph smoke, but it does not build or run `test_rate_utils`.
   Files:
   - `../vivid-glitch/.github/workflows/smoke.yml`
   - `../vivid-glitch/CMakeLists.txt`

Validation notes:

- README / AGENTS / manifest inspected
- workflow and test inventory inspected
- Package-local build passed
- Package C++ tests passed: 1/1
- Graph smoke from `vivid/build` passed 3 graphs and skipped 4 GPU-dependent graphs

Recommended fix order:

1. Replace empty manifest test arrays with real graph and C++ test entries
2. Add the package C++ test to the smoke workflow

#### `vivid-wavetable` — complete

Purpose / ownership summary:

- Audio synthesis package centered on `WavetableSynth`, with one core graph set and one optional extended graph set that depends on `vivid-sequencers`.

Package contract surface:

- Manifest: `vivid-package.json`
- Build integration: package-local `CMakeLists.txt`
- Graphs/examples: 2 core graphs + 2 extended graphs
- Tests: package manifest + wavetable interpolation + position-env integration test

Findings identified:

1. `P1` The manifest does not declare the repo's package tests or graph smoke assets, so package-manager-driven test execution cannot exercise the package's real validation set.
   Files:
   - `../vivid-wavetable/vivid-package.json`
   - `../vivid-wavetable/tests/cpp/test_wavetable_interp.cpp`
   - `../vivid-wavetable/tests/test_wavetable_position_env.cpp`
   - `../vivid-wavetable/graphs/core/wavetable_midi_demo.json`
2. `P2` The smoke workflow only builds and runs `test_package_manifest`; it skips the substantive wavetable interpolation and position-env tests.
   Files:
   - `../vivid-wavetable/.github/workflows/smoke.yml`
   - `../vivid-wavetable/CMakeLists.txt`

Validation notes:

- README / AGENTS / manifest inspected
- workflow and test inventory inspected
- Package-local build passed
- Package C++ tests passed: 1/1
- Graph smoke from `vivid/build` passed 2/2 core graphs after adding a headless-safe core smoke graph and keeping the MIDI graph as an interactive example

Recommended fix order:

1. Add manifest `tests` entries for core/extended graphs and C++ tests
2. Run the package-owned wavetable tests in CI before graph smoke

Highest-priority ecosystem risks:

1. Manifest test metadata is inconsistent with the real package-owned validation assets across nearly the entire sibling set.
2. Package-local smoke workflows routinely skip package-owned C++ tests and therefore do not match the validation shape implied by the repos themselves.
3. The remaining ecosystem gap is mostly structural: the generic `PackageTestRunner` still cannot reproduce every package-local CMake/CTest environment, so manifest `cpp` entries remain intentionally conservative in a few repos.

Recommended fix order across repos:

1. Preserve the new manifest `tests` metadata across sibling repos as the canonical package-test contract
2. Keep package smoke workflows running substantive package-owned C++ tests, not just manifest checks / graph smoke
3. Expand the generic `PackageTestRunner` if we want manifest `cpp` coverage to include heavier package-local CMake test targets
4. Opportunistically align remaining package docs (`AGENTS.md`, README validation sections) with the now-standardized validation contract

## After That

Planned next slices:

1. Control-server and package workflow audit
2. UI/runtime seam audit
3. Export, capture, settings, and release/runtime-update audit

## Notes

- This tracker is intentionally lightweight and current-state focused.
- Exploration context remains in the `CODE-REVIEW-PHASE*.md` notes.
- Findings should be copied here only after they are concrete enough to affect audit ordering or require fixes.
