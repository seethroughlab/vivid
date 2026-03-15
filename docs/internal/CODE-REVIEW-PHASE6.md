# Code Review Phase 6: Test Suite Exploration

## Purpose

This note is the Phase 6 test-suite artifact for the Vivid code review process described in [CODE_REVIEW.md](/Users/jeff/Developer/vivid/docs/CODE_REVIEW.md).

The goal of this phase is to understand the current shape of the automated and manual test surfaces before judging adequacy. This is still exploration rather than audit. It records:

- how the automated test suite is assembled
- what kinds of tests exist and what they appear to target
- where the suite is dense versus where it looks thinner at a glance
- how automated tests and manual test plans relate to each other
- which later review questions should be grounded in test coverage rather than only code reading

This note does not conclude whether coverage is sufficient.

## Test Surface Overview

At a high level, the current test surface appears to have three layers:

- **automated C++ tests** registered directly from the top-level [CMakeLists.txt](/Users/jeff/Developer/vivid/CMakeLists.txt)
- **graph fixtures and test-only operators** under [tests/graphs](/Users/jeff/Developer/vivid/tests/graphs) and [tests/operators](/Users/jeff/Developer/vivid/tests/operators)
- **manual validation plans** under [docs/testing](/Users/jeff/Developer/vivid/docs/testing)

The current built test inventory reports **59 registered CTest tests** in `build`.

This test architecture is important because it suggests Vivid does not use one monolithic testing framework layer. Instead, it assembles many focused executables around runtime subsystems, operator contracts, graph fixtures, and product workflows.

## How The Suite Is Assembled

### 1. Top-level CMake registration

**Primary file**
- [CMakeLists.txt](/Users/jeff/Developer/vivid/CMakeLists.txt)

The automated suite appears to be declared directly in the root build file rather than a separate `tests/CMakeLists.txt`.

That file currently:

- builds test-only operators used by runtime/scheduler/audio tests
- configures graph fixture JSON files into the build directory
- compiles individual test executables with the specific runtime sources they need
- registers each executable with CTest using `add_test(...)`
- marks at least some tests with labels such as `SMOKE`

This means the test suite is tightly coupled to the main build graph and visible from the same file that assembles the product.

### 2. Test fixture folders

**Primary paths**
- [tests/operators](/Users/jeff/Developer/vivid/tests/operators)
- [tests/graphs](/Users/jeff/Developer/vivid/tests/graphs)
- [tests/test_helpers.h](/Users/jeff/Developer/vivid/tests/test_helpers.h)

The fixture layer appears to serve two main purposes:

- small test operators used to exercise loader/scheduler/audio/spread/string semantics
- graph JSON fixtures used for runtime API, reload, spread, and audio-domain integration tests

This suggests many tests are integration-flavored even when they are fairly small, because they construct realistic runtime paths rather than only isolated pure functions.

### 3. Manual test docs

**Primary path**
- [docs/testing](/Users/jeff/Developer/vivid/docs/testing)

The testing docs appear to hold:

- manual validation catalogs
- inner/outer loop testing plans
- package smoke guidance
- operator-creation MCP test plans

From [docs/testing/README.md](/Users/jeff/Developer/vivid/docs/testing/README.md) and [MANUAL-TEST-CATALOG.md](/Users/jeff/Developer/vivid/docs/testing/MANUAL-TEST-CATALOG.md), the repo clearly treats manual testing as a planned complement to automation rather than an afterthought.

## Automated Test Topology

The current CTest inventory appears to cluster into several recognizable groups.

### 1. Core runtime and graph model tests

Representative tests:

- [test_graph.cpp](/Users/jeff/Developer/vivid/tests/test_graph.cpp)
- [test_scheduler.cpp](/Users/jeff/Developer/vivid/tests/test_scheduler.cpp)
- [test_runtime_api.cpp](/Users/jeff/Developer/vivid/tests/test_runtime_api.cpp)
- [test_control_server.cpp](/Users/jeff/Developer/vivid/tests/test_control_server.cpp)
- [test_hot_reload.cpp](/Users/jeff/Developer/vivid/tests/test_hot_reload.cpp)
- [test_hot_reloader_queue.cpp](/Users/jeff/Developer/vivid/tests/test_hot_reloader_queue.cpp)
- [test_operator_loader.cpp](/Users/jeff/Developer/vivid/tests/test_operator_loader.cpp)
- [test_builtin_operators.cpp](/Users/jeff/Developer/vivid/tests/test_builtin_operators.cpp)
- [test_operator_info_cache.cpp](/Users/jeff/Developer/vivid/tests/test_operator_info_cache.cpp)

What this group appears to cover:

- graph persistence and model behavior
- runtime mutation and rebuild flows
- loader/registry/hot-reload paths
- control-server command handling
- built-in type/metadata availability

This is one of the densest clusters in the automated suite.

### 2. Audio, scheduler, and cross-domain execution tests

Representative tests:

- [test_audio_engine.cpp](/Users/jeff/Developer/vivid/tests/test_audio_engine.cpp)
- [test_audio_robustness.cpp](/Users/jeff/Developer/vivid/tests/test_audio_robustness.cpp)
- [test_audio_domain_sequencer.cpp](/Users/jeff/Developer/vivid/tests/test_audio_domain_sequencer.cpp)
- [test_cross_domain_spread.cpp](/Users/jeff/Developer/vivid/tests/test_cross_domain_spread.cpp)
- [test_spread_broadcast.cpp](/Users/jeff/Developer/vivid/tests/test_spread_broadcast.cpp)
- [test_audio_spread_wire.cpp](/Users/jeff/Developer/vivid/tests/test_audio_spread_wire.cpp)
- [test_string_ports.cpp](/Users/jeff/Developer/vivid/tests/test_string_ports.cpp)
- [test_media_clock.cpp](/Users/jeff/Developer/vivid/tests/test_media_clock.cpp)
- [test_media_session_queue.cpp](/Users/jeff/Developer/vivid/tests/test_media_session_queue.cpp)

What this group appears to cover:

- audio execution and callback-facing behavior
- robustness around failure/throwing operators
- cross-domain value transport
- spread and string dataflow contracts
- media clock/session queue behavior

This group looks like the main automated surface for domain-boundary semantics.

### 3. Operator API and authoring-contract tests

Representative tests:

- [test_operator_creator.cpp](/Users/jeff/Developer/vivid/tests/test_operator_creator.cpp)
- [test_child_op.cpp](/Users/jeff/Developer/vivid/tests/test_child_op.cpp)
- [test_audio_dsp_api.cpp](/Users/jeff/Developer/vivid/tests/test_audio_dsp_api.cpp)
- [test_wgsl_header.cpp](/Users/jeff/Developer/vivid/tests/test_wgsl_header.cpp)
- [test_wgsl_preprocessor.cpp](/Users/jeff/Developer/vivid/tests/test_wgsl_preprocessor.cpp)
- [test_port_type_registry.cpp](/Users/jeff/Developer/vivid/tests/test_port_type_registry.cpp)
- [test_semantic_tags.cpp](/Users/jeff/Developer/vivid/tests/test_semantic_tags.cpp)

What this group appears to cover:

- operator scaffolding and creation behavior
- reusable operator-authoring helpers
- shader authoring support
- custom port type registration
- semantic-parameter metadata behavior

This looks like the main automated guardrail for the operator authoring contract.

### 4. GPU, movie, and media-path tests

Representative tests:

- [test_gpu_operators.cpp](/Users/jeff/Developer/vivid/tests/test_gpu_operators.cpp)
- [test_hap_codec.cpp](/Users/jeff/Developer/vivid/tests/test_hap_codec.cpp)
- [test_movie_decode_upload.cpp](/Users/jeff/Developer/vivid/tests/test_movie_decode_upload.cpp)
- [test_movie_load_generation.cpp](/Users/jeff/Developer/vivid/tests/test_movie_load_generation.cpp)
- [test_movie_load_async.cpp](/Users/jeff/Developer/vivid/tests/test_movie_load_async.cpp)
- [test_movie_long_loop_sync.cpp](/Users/jeff/Developer/vivid/tests/test_movie_long_loop_sync.cpp)
- [test_movie_decode_route.cpp](/Users/jeff/Developer/vivid/tests/test_movie_decode_route.cpp)
- [test_capture_coordinator.cpp](/Users/jeff/Developer/vivid/tests/test_capture_coordinator.cpp)

What this group appears to cover:

- core GPU operator behavior
- media codec routing and upload math
- async movie-load lifecycle behavior
- long-loop sync behavior
- capture/recording coordination

This is a substantial cluster for the newer movie/media path rather than just a single smoke test.

### 5. UI and presentation-support tests

Representative tests:

- [test_overlay_layouts.cpp](/Users/jeff/Developer/vivid/tests/test_overlay_layouts.cpp)
- [test_ui_overlay_interactions.cpp](/Users/jeff/Developer/vivid/tests/test_ui_overlay_interactions.cpp)
- [test_ui_arch_guard.cpp](/Users/jeff/Developer/vivid/tests/test_ui_arch_guard.cpp)
- [test_theme_loader.cpp](/Users/jeff/Developer/vivid/tests/test_theme_loader.cpp)
- [test_editor_detect.cpp](/Users/jeff/Developer/vivid/tests/test_editor_detect.cpp)

What this group appears to cover:

- overlay geometry and interaction behavior
- some UI architectural boundary enforcement
- theme loading
- editor detection/preferences support

Compared with runtime and package tests, this cluster appears smaller and more focused on guardrails/support logic than on full editor behavior.

### 6. Package and ecosystem tests

Representative tests:

- [test_package_compiler.cpp](/Users/jeff/Developer/vivid/tests/test_package_compiler.cpp)
- [test_package_catalog.cpp](/Users/jeff/Developer/vivid/tests/test_package_catalog.cpp)
- [test_package_manager.cpp](/Users/jeff/Developer/vivid/tests/test_package_manager.cpp)
- [test_package_scope_resolver.cpp](/Users/jeff/Developer/vivid/tests/test_package_scope_resolver.cpp)
- [test_package_scope_registry.cpp](/Users/jeff/Developer/vivid/tests/test_package_scope_registry.cpp)
- [test_package_scaffolder.cpp](/Users/jeff/Developer/vivid/tests/test_package_scaffolder.cpp)
- [test_package_update_logic.cpp](/Users/jeff/Developer/vivid/tests/test_package_update_logic.cpp)
- [test_package_test_runner.cpp](/Users/jeff/Developer/vivid/tests/test_package_test_runner.cpp)
- [test_app_update_manager.cpp](/Users/jeff/Developer/vivid/tests/test_app_update_manager.cpp)

What this group appears to cover:

- package build and manifest behavior
- scope resolution and package precedence
- scaffolding and update logic
- package-test execution
- core app-update metadata parsing

This is one of the clearest horizontal-coverage clusters in the repo.

### 7. Utility and support tests

Representative tests:

- [test_topo_sort.cpp](/Users/jeff/Developer/vivid/tests/test_topo_sort.cpp)
- [test_settings.cpp](/Users/jeff/Developer/vivid/tests/test_settings.cpp)
- [test_string_util.cpp](/Users/jeff/Developer/vivid/tests/test_string_util.cpp)
- [test_path_util.cpp](/Users/jeff/Developer/vivid/tests/test_path_util.cpp)
- [test_file_watcher.cpp](/Users/jeff/Developer/vivid/tests/test_file_watcher.cpp)
- [test_midi.cpp](/Users/jeff/Developer/vivid/tests/test_midi.cpp)
- [test_undo_manager.cpp](/Users/jeff/Developer/vivid/tests/test_undo_manager.cpp)
- [test_undo_mutation_types.cpp](/Users/jeff/Developer/vivid/tests/test_undo_mutation_types.cpp)
- [test_team_workflow_regression.cpp](/Users/jeff/Developer/vivid/tests/test_team_workflow_regression.cpp)

What this group appears to cover:

- common utilities
- persistence helpers
- support services such as MIDI and file watching
- undo behavior
- workflow-specific regressions

These tests appear to provide targeted guardrails around supporting infrastructure rather than one broader integration slice.

### 8. Graph/demo smoke tests

Representative test:

- [test_demo_graphs.cpp](/Users/jeff/Developer/vivid/tests/test_demo_graphs.cpp)

Supporting fixtures:

- [tests/graphs](/Users/jeff/Developer/vivid/tests/graphs)
- build-copied graph fixtures configured in [CMakeLists.txt](/Users/jeff/Developer/vivid/CMakeLists.txt)

This test appears to be the main automated smoke layer for graph examples and runtime loading paths. It is also one of the few tests explicitly labeled `SMOKE` in the build registration.

## Manual Test Surface

The repo’s manual testing docs suggest a parallel validation layer for areas that are harder to capture fully in small automated executables.

### 1. Manual functional catalog

**Primary file**
- [MANUAL-TEST-CATALOG.md](/Users/jeff/Developer/vivid/docs/testing/MANUAL-TEST-CATALOG.md)

This document covers areas such as:

- graph editing
- parameter UI
- audio output and device handling
- GPU rendering and shader errors
- package install/uninstall from UI
- file I/O and file association
- MIDI input/learn/hot-plug
- variations and presets
- capture
- themes
- fullscreen and external display

This suggests the manual layer is aimed at whole-product interaction flows and hardware/platform behavior rather than the lower-level contracts that dominate the automated suite.

### 2. Additional testing docs

Other docs in [docs/testing](/Users/jeff/Developer/vivid/docs/testing) suggest additional process-oriented testing surfaces:

- [INNER-OUTER-LOOP-TEST-PLAN.md](/Users/jeff/Developer/vivid/docs/testing/INNER-OUTER-LOOP-TEST-PLAN.md)
- [PACKAGE-SMOKE-TEST.md](/Users/jeff/Developer/vivid/docs/testing/PACKAGE-SMOKE-TEST.md)
- [OPERATOR-CREATION-MCP-TEST-PLAN.md](/Users/jeff/Developer/vivid/docs/testing/OPERATOR-CREATION-MCP-TEST-PLAN.md)

These reinforce that testing in Vivid is not only unit/integration automation. There is also explicit process documentation for authoring, package, and outer-loop workflows.

## Coverage Shape By Architecture Area

This section only describes the apparent shape of coverage, not its adequacy.

### Dense-looking areas

From filenames and CTest registration, the following areas appear relatively dense:

- runtime core and graph mutation surfaces
- package lifecycle and ecosystem services
- audio/cross-domain scheduler behavior
- operator loader/creator and authoring-contract support
- movie/media lifecycle and codec-route behavior

### Moderate-looking areas

The following areas appear to have visible but narrower automated coverage:

- UI overlays and presentation-support helpers
- capture/export-adjacent behavior
- settings and support utilities
- semantic-tag and custom-port contract behavior

### Thin-looking or manual-heavy areas

The following areas appear more dependent on manual or smoke-style validation at this phase of exploration:

- full editor interaction flows beyond targeted overlay tests
- broad end-to-end visual UX behavior
- platform integration details such as file association, fullscreen, external display, and some device behavior
- release/packaging outer-loop workflows

This is only an exploration impression. Later audit phases should verify these impressions against actual test content and recent CI usage.

## Test Infrastructure Patterns

A few structural patterns are visible from this phase.

### 1. Small bespoke executables rather than one shared framework

The suite appears to prefer many focused test binaries over one central runner. This likely makes subsystem dependencies explicit, but it also means coverage understanding depends heavily on naming and CMake registration.

### 2. Real runtime sources linked into tests

Many tests compile against real runtime modules rather than isolated mocks. This suggests a bias toward subsystem-integration testing over extremely narrow pure-unit isolation.

### 3. Test-only operator plugins and graph fixtures

The use of [tests/operators](/Users/jeff/Developer/vivid/tests/operators) and [tests/graphs](/Users/jeff/Developer/vivid/tests/graphs) suggests the suite often exercises the same loader/scheduler/graph machinery used in the app, which is especially relevant for a plugin-oriented architecture.

### 4. Manual docs as an intended complement

The manual testing docs are clearly part of the intended verification strategy, not just leftover notes. This matters for later audit because some product areas are expected to be validated outside CTest.

## Open Questions For Phase 7+

These are exploration questions, not findings.

- Which tests are fast inner-loop guards versus slower integration or smoke tests in everyday use?
- Which of the 59 registered tests run in CI by default, and which are more opportunistic?
- How much of the UI/editor behavior is intentionally covered by manual testing rather than automation?
- Are there architecture areas mapped in earlier phases that have no dedicated automated test owner at all?
- Which manual testing docs correspond to recurring release gates versus informal engineering guidance?

## Phase Boundary

This phase establishes the broad topology of the test surface:

- the automated suite is CTest-based and assembled in the top-level build
- coverage is organized as many focused executables rather than one centralized harness
- fixtures include test-only operators and graph JSONs, which fit the plugin-and-graph architecture well
- manual testing is a formal companion layer for outer-loop product behavior and platform workflows

The next phase should synthesize the earlier exploration artifacts into one review-preparation map: subsystem inventory, ownership boundaries, public contracts, test topology, and likely audit hotspots.
