# Code Review Phase 7: Synthesis Pass

## Purpose

This note is the Phase 7 synthesis artifact for the Vivid code review process described in [CODE_REVIEW.md](/Users/jeff/Developer/vivid/docs/CODE_REVIEW.md).

The goal of this phase is to consolidate the earlier exploration passes into one review-preparation map that can guide the actual code review campaign. This is still preparation rather than audit. It records:

- the current subsystem inventory
- the main ownership and integration boundaries
- the public and semi-public contracts that deserve review attention
- the current test-topology map
- the likely order and method for the real review work

This note does not yet report findings.

## What The Exploration Established

Across Phases 1 through 6, the repo now appears to have a coherent high-level structure built around a small set of strong centers:

- a runtime spine centered on `Graph`, `Scheduler`, `AudioEngine`, `OperatorRegistry`, and `RuntimeAPI`
- an operator-authoring contract centered on `src/operator_api` and expressed through seed operators in `operators/`
- a retained editor surface centered on `NodeGraphUI`, `GraphSnapshot`, and `UICommandSink`
- cross-cutting package, control-server, MCP, settings, update, capture, and export systems layered around the runtime
- a test suite composed of many focused executables, graph fixtures, and manual outer-loop validation docs

The most important outcome of the exploration is not just “what exists,” but “where the seams are.” Vivid appears to be reviewable because it has several meaningful architectural boundaries rather than one fully entangled codebase.

## Consolidated Subsystem Inventory

### 1. Runtime Spine

**Primary directories/files**
- [src/runtime](/Users/jeff/Developer/vivid/src/runtime)
- especially [main.cpp](/Users/jeff/Developer/vivid/src/runtime/main.cpp), [graph.h](/Users/jeff/Developer/vivid/src/runtime/graph.h), [scheduler.h](/Users/jeff/Developer/vivid/src/runtime/scheduler.h), [audio_engine.h](/Users/jeff/Developer/vivid/src/runtime/audio_engine.h), [runtime_api.h](/Users/jeff/Developer/vivid/src/runtime/runtime_api.h), [operator_registry.h](/Users/jeff/Developer/vivid/src/runtime/operator_registry.h), [operator_loader.h](/Users/jeff/Developer/vivid/src/runtime/operator_loader.h)

**What it owns**
- process assembly
- graph persistence and rebuild
- live control/GPU execution
- audio execution and cross-domain bridging
- operator availability/loading
- runtime mutation coordination

**Why it matters for review**
- this is the execution core the rest of the product depends on
- it defines ownership, rebuild semantics, and domain boundaries

### 2. Operator Contract and Seed Operator Surface

**Primary directories/files**
- [src/operator_api](/Users/jeff/Developer/vivid/src/operator_api)
- [operators/control](/Users/jeff/Developer/vivid/operators/control)
- [operators/audio](/Users/jeff/Developer/vivid/operators/audio)
- [operators/gpu](/Users/jeff/Developer/vivid/operators/gpu)
- [operators/shared](/Users/jeff/Developer/vivid/operators/shared)

**What it owns**
- the public authoring contract for operators
- the current seed-operator implementation patterns
- newer custom-port, string, and media/session flows

**Why it matters for review**
- Vivid’s product value depends heavily on operator authoring and composition
- contract clarity here affects both built-in behavior and package ecosystem growth

### 3. UI / Interaction Surface

**Primary directories/files**
- [src/ui](/Users/jeff/Developer/vivid/src/ui)
- especially [node_graph.h](/Users/jeff/Developer/vivid/src/ui/node_graph.h), [graph_snapshot.h](/Users/jeff/Developer/vivid/src/ui/graph_snapshot.h), [ui_command_sink.h](/Users/jeff/Developer/vivid/src/ui/ui_command_sink.h), [renderer_2d.h](/Users/jeff/Developer/vivid/src/ui/renderer_2d.h)

**What it owns**
- retained editor state
- graph editing and overlay flows
- runtime-fed read-model presentation
- UI mutation commands into the runtime
- renderer/thumbnails/themes/native dialogs

**Why it matters for review**
- the environment is central to the product thesis
- UI/runtime seams strongly affect whether interaction behavior stays understandable and stable

### 4. Cross-Cutting Product and Tooling Systems

**Primary directories/files**
- [src/runtime/control_server.h](/Users/jeff/Developer/vivid/src/runtime/control_server.h)
- [src/runtime/package_manager.h](/Users/jeff/Developer/vivid/src/runtime/package_manager.h)
- [src/runtime/package_compiler.h](/Users/jeff/Developer/vivid/src/runtime/package_compiler.h)
- [src/runtime/package_catalog.h](/Users/jeff/Developer/vivid/src/runtime/package_catalog.h)
- [src/runtime/hot_reload.h](/Users/jeff/Developer/vivid/src/runtime/hot_reload.h)
- [src/runtime/settings.h](/Users/jeff/Developer/vivid/src/runtime/settings.h)
- [src/runtime/app_update_manager.h](/Users/jeff/Developer/vivid/src/runtime/app_update_manager.h)
- [src/runtime/capture_coordinator.h](/Users/jeff/Developer/vivid/src/runtime/capture_coordinator.h)
- [src/export/export_pipeline.h](/Users/jeff/Developer/vivid/src/export/export_pipeline.h)
- [mcp/vivid_mcp.py](/Users/jeff/Developer/vivid/mcp/vivid_mcp.py)
- [mcp/vivid_opdev_mcp.py](/Users/jeff/Developer/vivid/mcp/vivid_opdev_mcp.py)

**What they own**
- external control and introspection surfaces
- package install/build/test/scaffold/catalog flows
- hot reload and dev loop support
- settings and update behavior
- capture and export surfaces
- LLM/MCP bridge layers

**Why they matter for review**
- they turn the runtime into a product ecosystem rather than only an app
- they define many of the external and operator-authoring workflows users actually experience

### 5. Test Surface

**Primary directories/files**
- [tests](/Users/jeff/Developer/vivid/tests)
- [docs/testing](/Users/jeff/Developer/vivid/docs/testing)
- [CMakeLists.txt](/Users/jeff/Developer/vivid/CMakeLists.txt)

**What it owns**
- focused automated test executables
- graph and operator fixtures
- manual validation catalogs and process docs

**Why it matters for review**
- it provides the map of what the codebase is already trying to protect
- it helps distinguish low-confidence areas from areas with many existing guardrails

## Main Ownership Boundaries

The exploration suggests a handful of boundaries that will matter more than individual implementation details during audit.

### 1. Persisted model vs live execution

The strongest ownership split in the runtime is:

- persisted model in [Graph](/Users/jeff/Developer/vivid/src/runtime/graph.h)
- live control/GPU execution in [Scheduler](/Users/jeff/Developer/vivid/src/runtime/scheduler.h)
- live audio execution in [AudioEngine](/Users/jeff/Developer/vivid/src/runtime/audio_engine.h)
- mutation coordination in [RuntimeAPI](/Users/jeff/Developer/vivid/src/runtime/runtime_api.h)

This boundary should anchor the runtime review.

### 2. Operator descriptor contract vs operator implementations

The second major split is:

- operator API contract in [src/operator_api](/Users/jeff/Developer/vivid/src/operator_api)
- concrete seed operator behavior in [operators](/Users/jeff/Developer/vivid/operators)
- runtime loader/registry interpretation in [operator_loader.h](/Users/jeff/Developer/vivid/src/runtime/operator_loader.h) and [operator_registry.h](/Users/jeff/Developer/vivid/src/runtime/operator_registry.h)

This boundary should anchor the operator review.

### 3. UI-owned state vs runtime-fed state vs runtime mutations

The clearest UI seam is:

- retained interaction state in [NodeGraphUI](/Users/jeff/Developer/vivid/src/ui/node_graph.h)
- presentation snapshot in [GraphSnapshot](/Users/jeff/Developer/vivid/src/ui/graph_snapshot.h)
- command boundary in [UICommandSink](/Users/jeff/Developer/vivid/src/ui/ui_command_sink.h)

This boundary should anchor the UI review.

### 4. Internal runtime truth vs external transport layers

The main external-surface split is:

- internal mutation/control truth in [RuntimeAPI](/Users/jeff/Developer/vivid/src/runtime/runtime_api.h)
- external HTTP aggregation in [ControlServer](/Users/jeff/Developer/vivid/src/runtime/control_server.h)
- tool-shaping bridges in [mcp/vivid_mcp.py](/Users/jeff/Developer/vivid/mcp/vivid_mcp.py) and [mcp/vivid_opdev_mcp.py](/Users/jeff/Developer/vivid/mcp/vivid_opdev_mcp.py)

This boundary should anchor review of API and tool surfaces.

### 5. Live runtime assembly vs exported/distributed assembly

A final important split is:

- live/plugin-based assembly in the runtime
- static standalone assembly in [ExportPipeline](/Users/jeff/Developer/vivid/src/export/export_pipeline.h)

This boundary should anchor any review of consistency between authoring-time and distribution-time behavior.

## Public and Semi-Public Contracts To Review Later

The exploration suggests that later audit work should treat the following as contracts, not just implementation details.

### Product/runtime contracts

- graph JSON model in [graph.h](/Users/jeff/Developer/vivid/src/runtime/graph.h)
- runtime mutation surface in [runtime_api.h](/Users/jeff/Developer/vivid/src/runtime/runtime_api.h)
- control-server endpoint behavior in [control_server.h](/Users/jeff/Developer/vivid/src/runtime/control_server.h)
- package manifest and scope behavior in [package_manager.h](/Users/jeff/Developer/vivid/src/runtime/package_manager.h)

### Operator contracts

- operator ABI and process contexts in [types.h](/Users/jeff/Developer/vivid/src/operator_api/types.h)
- registration/export contract in [operator.h](/Users/jeff/Developer/vivid/src/operator_api/operator.h)
- custom-port/type contract in [type_id.h](/Users/jeff/Developer/vivid/src/operator_api/type_id.h) and [port_type_registry.h](/Users/jeff/Developer/vivid/src/operator_api/port_type_registry.h)
- semantic-parameter metadata contract across API, runtime, and tooling

### UI contracts

- graph snapshot shape in [graph_snapshot.h](/Users/jeff/Developer/vivid/src/ui/graph_snapshot.h)
- UI command contract in [ui_command_sink.h](/Users/jeff/Developer/vivid/src/ui/ui_command_sink.h)
- overlay and browser behavior as reflected in the UI-layer file split

### External-tooling contracts

- MCP tool surfaces in [mcp/vivid_mcp.py](/Users/jeff/Developer/vivid/mcp/vivid_mcp.py) and [mcp/vivid_opdev_mcp.py](/Users/jeff/Developer/vivid/mcp/vivid_opdev_mcp.py)
- export assembly inputs and manifests in [export_pipeline.h](/Users/jeff/Developer/vivid/src/export/export_pipeline.h)
- settings/update behavior in [settings.h](/Users/jeff/Developer/vivid/src/runtime/settings.h) and [app_update_manager.h](/Users/jeff/Developer/vivid/src/runtime/app_update_manager.h)

## Consolidated Test Topology

The current automated/manual test map appears to support the architecture in the following way.

### Strong automated coverage clusters

Exploration suggests the densest automated clusters are:

- runtime core and graph mutation flows
- package lifecycle and ecosystem services
- audio/cross-domain contract behavior
- operator-loader/creator and authoring support
- movie/media-path behavior

### Targeted but narrower automated clusters

Exploration suggests these areas have more targeted automated coverage:

- UI overlays and presentation helpers
- theme/editor preference support
- custom-port and semantic-tag contract behavior
- capture/export-adjacent support

### Manual/outer-loop companion layer

The manual test docs appear to absorb more of the whole-product surface:

- broad graph editing behavior
- parameter UI interactions
- platform/device/hardware behavior
- package installation from UI
- fullscreen/external display
- file association and some outer-loop release behavior

This means the later code review should not treat automated tests alone as the whole validation story.

## Review Hotspot Shortlist

Based on the exploration only, the following areas appear most likely to deserve earlier and deeper audit passes.

### 1. Runtime mutation and rebuild boundary

**Primary files**
- [runtime_api.h](/Users/jeff/Developer/vivid/src/runtime/runtime_api.h)
- [scheduler.h](/Users/jeff/Developer/vivid/src/runtime/scheduler.h)
- [audio_engine.h](/Users/jeff/Developer/vivid/src/runtime/audio_engine.h)
- [main.cpp](/Users/jeff/Developer/vivid/src/runtime/main.cpp)

**Why first**
- this is where persisted graph state, live scheduler state, and live audio state must stay coherent

### 2. Operator contract and loader boundary

**Primary files**
- [src/operator_api](/Users/jeff/Developer/vivid/src/operator_api)
- [operator_loader.h](/Users/jeff/Developer/vivid/src/runtime/operator_loader.h)
- [operator_registry.h](/Users/jeff/Developer/vivid/src/runtime/operator_registry.h)

**Why early**
- this is the core authoring and package-extension surface for the product

### 3. Control-server and external surface aggregation

**Primary files**
- [control_server.h](/Users/jeff/Developer/vivid/src/runtime/control_server.h)
- [mcp/vivid_mcp.py](/Users/jeff/Developer/vivid/mcp/vivid_mcp.py)
- [mcp/vivid_opdev_mcp.py](/Users/jeff/Developer/vivid/mcp/vivid_opdev_mcp.py)

**Why early**
- this is where many runtime capabilities become real external contracts

### 4. Package lifecycle and operator-development workflows

**Primary files**
- [package_manager.h](/Users/jeff/Developer/vivid/src/runtime/package_manager.h)
- [package_compiler.h](/Users/jeff/Developer/vivid/src/runtime/package_compiler.h)
- [package_catalog.h](/Users/jeff/Developer/vivid/src/runtime/package_catalog.h)
- [package_test_runner.h](/Users/jeff/Developer/vivid/src/runtime/package_test_runner.h)
- [package_scaffolder.h](/Users/jeff/Developer/vivid/src/runtime/package_scaffolder.h)

**Why early**
- package workflows are central to the project’s ecosystem strategy and touch build/runtime boundaries

### 5. UI controller and UI/runtime seams

**Primary files**
- [node_graph.h](/Users/jeff/Developer/vivid/src/ui/node_graph.h)
- [graph_snapshot.h](/Users/jeff/Developer/vivid/src/ui/graph_snapshot.h)
- [ui_command_sink.h](/Users/jeff/Developer/vivid/src/ui/ui_command_sink.h)

**Why later but still high-value**
- the UI is central to the product, but the most valuable early static review likely starts with runtime and contracts before editor behavior

## Recommended Audit Sequence

This is the proposed order for the actual code review campaign.

### Pass 1: Runtime consistency review

Focus on:

- `Graph`, `Scheduler`, `AudioEngine`, `RuntimeAPI`, `main.cpp`
- ownership, rebuild semantics, live-vs-persisted state alignment
- cross-domain data movement

Method:

- static reading first
- then targeted runtime validation where mutation/rebuild behavior is not obvious from code alone

### Pass 2: Operator contract and loader review

Focus on:

- `src/operator_api`
- `operator_loader`, `operator_registry`
- representative seed operators from the Phase 3 shortlist

Method:

- static review anchored in the contract files
- compare contract expectations against real seed-operator usage

### Pass 3: External surface and package workflow review

Focus on:

- `ControlServer`
- MCP bridge files
- package manager/compiler/catalog/test/scaffold services

Method:

- static review of API surfaces
- targeted behavioral verification where external contracts, background work, or package precedence are involved

### Pass 4: UI and interaction review

Focus on:

- `NodeGraphUI`
- snapshot/command boundaries
- overlay/browser interactions
- renderer/thumbnails only insofar as they affect editor behavior

Method:

- static reading of state ownership and file boundaries
- likely paired with manual runtime testing for key interaction flows

### Pass 5: Export, capture, settings, and release-path review

Focus on:

- export pipeline
- capture coordinator
- settings/update flows
- remaining cross-cutting release/distribution surfaces

Method:

- static review plus focused end-to-end checks where assembly/output behavior matters

## Where Static Reading Will Not Be Enough

The exploration suggests several areas where later review should expect to combine code reading with behavior verification.

### 1. Rebuild/reload and async lifecycle behavior

This includes:

- topology rebuilds
- operator hot reload
- package rebuild paths
- movie/media async load flows

### 2. UI interaction behavior

This includes:

- overlay and modal focus behavior
- drag/select/edit flows
- snapshot-to-command synchronization in the live editor

### 3. Hardware/platform and outer-loop behavior

This includes:

- audio device behavior
- MIDI hot-plug and learn flows
- fullscreen/external display
- file association and some capture/export workflows

### 4. Package ecosystem and external-tool surfaces

This includes:

- package scope/precedence behavior in practice
- MCP/control-server surface consistency
- export/runtime consistency for real graphs

## Open Questions Carried Forward

The main unresolved questions from the exploration, to be answered during audit rather than before it, are:

- Where do doc/code drifts matter most in active subsystems?
- Which external surfaces should be treated as hard public contracts versus internal implementation details?
- Which UI/editor behaviors are intentionally manual-test territory versus plausible automation targets?
- Which runtime or package paths are sufficiently specified in code versus specified mostly by convention and tests?
- Where does current test density meaningfully reduce audit risk, and where does it not?

## Phase Outcome

This synthesis pass produces a review-ready map of the repo:

- the main subsystem inventory is established
- the strongest ownership and integration boundaries are identified
- the key contracts worth reviewing are listed explicitly
- the current test topology is mapped alongside the architecture
- a concrete audit sequence now exists for the actual review campaign

At this point, the exploration stage is complete enough to begin real code review work in a structured order rather than continuing to rediscover the repo shape.
