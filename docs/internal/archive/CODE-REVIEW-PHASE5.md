# Code Review Phase 5: Cross-Cutting Systems Exploration

## Purpose

This note is the Phase 5 cross-cutting-systems artifact for the Vivid code review process described in [CODE_REVIEW.md](/Users/jeff/Developer/vivid/docs/internal/CODE_REVIEW.md).

The goal of this phase is to map the systems that cut across the runtime, UI, operator surface, packaging, and external tooling rather than belonging to a single vertical subsystem. This is still exploration rather than audit. It records:

- the main external interface surfaces around the runtime
- the package/discovery/build/test lifecycle as a horizontal slice through the app
- process-wide services such as settings, updates, capture, and export
- where the MCP/LLM bridge sits relative to the runtime control surface
- the main integration boundaries that later review should focus on

This note does not judge reliability, security, or product quality.

## Cross-Cutting Overview

At a high level, the current cross-cutting systems appear to cluster into five groups:

- **external runtime control** through [ControlServer](/Users/jeff/Developer/vivid/src/runtime/control_server.h), [RuntimeAPI](/Users/jeff/Developer/vivid/src/runtime/runtime_api.h), and the Python MCP bridges in [mcp/vivid_mcp.py](/Users/jeff/Developer/vivid/mcp/vivid_mcp.py) and [mcp/vivid_opdev_mcp.py](/Users/jeff/Developer/vivid/mcp/vivid_opdev_mcp.py)
- **package lifecycle and ecosystem services** through [package_manager.h](/Users/jeff/Developer/vivid/src/runtime/package_manager.h), [package_compiler.h](/Users/jeff/Developer/vivid/src/runtime/package_compiler.h), [package_catalog.h](/Users/jeff/Developer/vivid/src/runtime/package_catalog.h), [package_test_runner.h](/Users/jeff/Developer/vivid/src/runtime/package_test_runner.h), and [package_scaffolder.h](/Users/jeff/Developer/vivid/src/runtime/package_scaffolder.h)
- **live-edit and development tooling** through [hot_reload.h](/Users/jeff/Developer/vivid/src/runtime/hot_reload.h) and operator-creation flows exposed through control-server/MCP surfaces
- **process-level persistence and update behavior** through [settings.h](/Users/jeff/Developer/vivid/src/runtime/settings.h) and [app_update_manager.h](/Users/jeff/Developer/vivid/src/runtime/app_update_manager.h)
- **capture and distribution flows** through [capture_coordinator.h](/Users/jeff/Developer/vivid/src/runtime/capture_coordinator.h) and [export_pipeline.h](/Users/jeff/Developer/vivid/src/export/export_pipeline.h)

Unlike the earlier phases, these systems do not define one core runtime loop or one primary UI controller. Instead, they appear to sit at the edges of the runtime and expose, feed, rebuild, package, or persist it.

## External Interface Surface

### 1. Runtime command surface

**Primary file**
- [runtime_api.h](/Users/jeff/Developer/vivid/src/runtime/runtime_api.h)

[RuntimeAPI](/Users/jeff/Developer/vivid/src/runtime/runtime_api.h) appears to be the central mutation and persistence surface shared by several higher-level systems.

It exposes operations for:

- live parameter and string-param updates
- topology changes and buffered graph rebuilds
- layout and resolution persistence
- MIDI mappings
- variations and per-node presets
- graph save/reload/new-project flows
- solo state and graph dirty tracking

This makes it look like the internal command surface that other horizontal systems build on rather than bypass.

### 2. HTTP control surface

**Primary files**
- [control_server.h](/Users/jeff/Developer/vivid/src/runtime/control_server.h)
- [control_server.cpp](/Users/jeff/Developer/vivid/src/runtime/control_server.cpp)

[ControlServer](/Users/jeff/Developer/vivid/src/runtime/control_server.h) appears to be the main external integration hub.

From the header and implementation shape, it appears to aggregate access to:

- runtime graph mutation through `RuntimeAPI`
- graph and scheduler inspection
- operator/type/catalog introspection
- package install/rebuild/link/unlink operations
- hot reload hooks
- capture and recording actions
- settings-backed behavior
- core-app update checks and package-catalog data
- operator creation/cloning/scaffolding flows

This is a broader integration surface than a thin RPC wrapper. It appears to be the place where many otherwise separate runtime services become externally accessible through one HTTP boundary.

## MCP / LLM Bridge Surface

### 1. Graph/runtime MCP bridge

**Primary files**
- [mcp/vivid_mcp.py](/Users/jeff/Developer/vivid/mcp/vivid_mcp.py)
- [docs/LLM-INTEGRATION.md](/Users/jeff/Developer/vivid/docs/LLM-INTEGRATION.md)

The graph/runtime MCP server appears to sit above the HTTP control server rather than beside it.

Its role appears to be:

- expose runtime operations as MCP tools
- provide compact, deterministic envelopes for introspection/diagnostics/checks
- package common graph-edit actions for LLM workflows
- keep the external LLM tool surface aligned to runtime capabilities without embedding model-specific logic into the runtime itself

This suggests the LLM integration path is an adapter over the control-server surface rather than a separate execution stack.

### 2. Operator-development MCP bridge

**Primary file**
- [mcp/vivid_opdev_mcp.py](/Users/jeff/Developer/vivid/mcp/vivid_opdev_mcp.py)

The operator-development MCP server appears to combine two roles:

- documentation/resource serving for operator API headers and curated authoring docs
- forwarding action requests such as scaffolding, rebuild, link, and test into the same runtime/control surfaces

This makes it a second horizontal bridge: less about live graph control, more about package/operator development workflows.

### 3. LLM architecture model

**Primary doc**
- [LLM-INTEGRATION.md](/Users/jeff/Developer/vivid/docs/LLM-INTEGRATION.md)

The current docs describe a layered model in which:

- `RuntimeAPI` is the internal source of truth
- the MCP bridge is the primary LLM integration path
- the built-in chat path is deferred
- future WebSocket surfaces may reuse the same underlying runtime capabilities

For later review, this means the LLM bridge should be examined primarily as a transport and tool-shaping layer, not as a second source of runtime truth.

## Package and Ecosystem Surface

### 1. Package lifecycle manager

**Primary file**
- [package_manager.h](/Users/jeff/Developer/vivid/src/runtime/package_manager.h)

[PackageManager](/Users/jeff/Developer/vivid/src/runtime/package_manager.h) appears to be the central package orchestration layer.

It owns or coordinates:

- install from URL or path
- uninstall
- link/unlink for development
- rebuild of installed or linked packages
- package discovery across scopes
- manifest parsing
- package-path resolution
- dependency tracking and install chaining
- scan-at-startup into the operator registry
- version/update assessment helpers

This is more than a filesystem wrapper. It appears to be the package ecosystem entry point that binds manifests, package precedence, compilation, and registry loading together.

### 2. Package build surface

**Primary file**
- [package_compiler.h](/Users/jeff/Developer/vivid/src/runtime/package_compiler.h)

[PackageCompiler](/Users/jeff/Developer/vivid/src/runtime/package_compiler.h) appears to be the build-system adapter for packages.

It exposes:

- compile one operator
- compile all operators from a package manifest
- compile package C++ tests
- access to Vivid source/build roots for generated compile commands

This makes it a key seam between the package model and the local toolchain.

### 3. Package catalog/update surface

**Primary file**
- [package_catalog.h](/Users/jeff/Developer/vivid/src/runtime/package_catalog.h)

[PackageCatalog](/Users/jeff/Developer/vivid/src/runtime/package_catalog.h) appears to layer remote discovery/update data over local package state.

It appears to handle:

- background fetch of package index data
- cached catalog persistence
- merged installed/remote state
- update summaries for the UI and control surfaces
- install/uninstall by catalog identity

This is the main cross-cutting boundary between local package management and remote ecosystem metadata.

### 4. Package test and scaffold support

**Primary files**
- [package_test_runner.h](/Users/jeff/Developer/vivid/src/runtime/package_test_runner.h)
- [package_scaffolder.h](/Users/jeff/Developer/vivid/src/runtime/package_scaffolder.h)

These appear to round out the package lifecycle:

- `PackageTestRunner` runs graph and C++ package tests through package-manager/compiler/registry services
- `PackageScaffolder` generates package templates and validates package names

Together with package manager/compiler/catalog, these files make the package ecosystem look like a fairly complete horizontal toolchain rather than just a plugin loader.

## Live-Edit and Development Tooling Surface

### 1. Hot reload

**Primary file**
- [hot_reload.h](/Users/jeff/Developer/vivid/src/runtime/hot_reload.h)

[HotReloader](/Users/jeff/Developer/vivid/src/runtime/hot_reload.h) appears to be the process-local async build/reload queue.

It owns:

- queued rebuild requests
- build-thread lifecycle
- result staging
- deduping and in-flight tracking
- package-target callback integration for non-core builds

This is a horizontal system because it ties together:

- source edits
- CMake/package compilation
- staged dylib outputs
- runtime operator reloading on the main thread

### 2. Operator creation / authoring workflows

Although the full operator-creation path spans several runtime files, the cross-cutting surface is visible here through:

- `ControlServer` action endpoints
- `RuntimeAPI` mutation model
- `PackageScaffolder`
- the operator-development MCP bridge

This suggests authoring flows are intentionally integrated with the same package/build/control surface rather than split into a separate developer-only tool.

## Persistence, Updates, and Process-Level State

### 1. Settings

**Primary file**
- [settings.h](/Users/jeff/Developer/vivid/src/runtime/settings.h)

[Settings](/Users/jeff/Developer/vivid/src/runtime/settings.h) appears to hold user/process preferences that cut across UI, editor integration, release/update policy, workspace policy, and operator-destination behavior.

Current fields include:

- window geometry
- graph-wire display preferences
- external editor preference and command template
- selected UI style
- core-update auto-check and skipped-version state
- workspace root and workspace seeding version
- operator clone destination policy and project-root hints

This is a good example of a genuinely cross-cutting state object: it influences UI, package/authoring flows, update checks, and filesystem behavior.

### 2. Core app update manager

**Primary file**
- [app_update_manager.h](/Users/jeff/Developer/vivid/src/runtime/app_update_manager.h)

[AppUpdateManager](/Users/jeff/Developer/vivid/src/runtime/app_update_manager.h) appears to own core-application update checks separately from the package catalog.

It appears to handle:

- background appcast fetch
- parser output for latest release info
- skipped-version suppression
- state snapshots for UI/control surfaces

This creates a second remote-metadata path alongside package catalog behavior: one for the app, one for the ecosystem.

## Capture and Distribution Surface

### 1. Capture / recording coordination

**Primary files**
- [capture_coordinator.h](/Users/jeff/Developer/vivid/src/runtime/capture_coordinator.h)
- [av_exporter.h](/Users/jeff/Developer/vivid/src/runtime/av_exporter.h)

[CaptureCoordinator](/Users/jeff/Developer/vivid/src/runtime/capture_coordinator.h) appears to bridge the runtime and output/capture workflows.

It appears to manage:

- asynchronous capture requests from HTTP/UI layers
- frame capture from GPU textures
- audio capture via the audio engine
- AV recording through an exporter object
- snapshot-to-file requests
- recording-state reporting back to UI/control surfaces

This is cross-cutting because it touches runtime rendering, audio output, control server access, and persisted files.

### 2. Standalone export pipeline

**Primary files**
- [export_pipeline.h](/Users/jeff/Developer/vivid/src/export/export_pipeline.h)
- [export_pipeline.cpp](/Users/jeff/Developer/vivid/src/export/export_pipeline.cpp)
- [standalone_main.cpp](/Users/jeff/Developer/vivid/src/export/standalone_main.cpp)
- [standalone.cmake.in](/Users/jeff/Developer/vivid/src/export/standalone.cmake.in)

[ExportPipeline](/Users/jeff/Developer/vivid/src/export/export_pipeline.h) appears to be the distribution-time counterpart to the live package/runtime system.

It appears to:

- resolve which operators a graph needs
- load an operator manifest from the build tree
- generate static operator registration for export
- embed graph data and WGSL preset data
- generate a standalone CMake project
- build and copy the exported binary

This makes export another major horizontal slice: it reuses graph and operator concepts, but changes the assembly model from live-plugin runtime to static standalone build output.

## External-Surface Inventory

The main external or semi-external surfaces visible in this phase are:

- HTTP runtime control in [control_server.h](/Users/jeff/Developer/vivid/src/runtime/control_server.h)
- MCP bridges in [mcp/vivid_mcp.py](/Users/jeff/Developer/vivid/mcp/vivid_mcp.py) and [mcp/vivid_opdev_mcp.py](/Users/jeff/Developer/vivid/mcp/vivid_opdev_mcp.py)
- package install/link/rebuild/test/scaffold through the package runtime services
- remote package metadata through [package_catalog.h](/Users/jeff/Developer/vivid/src/runtime/package_catalog.h)
- remote app update metadata through [app_update_manager.h](/Users/jeff/Developer/vivid/src/runtime/app_update_manager.h)
- file capture and recording through [capture_coordinator.h](/Users/jeff/Developer/vivid/src/runtime/capture_coordinator.h)
- standalone binary export through [export_pipeline.h](/Users/jeff/Developer/vivid/src/export/export_pipeline.h)

## Integration Boundary Summary

The most important boundary visible in this phase is that many seemingly separate product features appear to converge on a smaller set of shared foundations:

- `RuntimeAPI` for mutation and persistence
- `ControlServer` for external exposure and aggregation
- package services for ecosystem lifecycle and development workflows
- settings/update services for process-level behavior
- export/capture services for output and release surfaces

This means later review should not treat package management, MCP, export, and capture as isolated features. They appear to be parallel slices through the same runtime core.

## Architectural Choke Points

These are not findings. They are the cross-cutting areas later review is most likely to hinge on.

### 1. `ControlServer` as an integration aggregator

[ControlServer](/Users/jeff/Developer/vivid/src/runtime/control_server.h) appears to concentrate many external-facing responsibilities in one surface. Later review should treat it as a likely coupling hotspot and as the canonical map of externally reachable runtime capabilities.

### 2. `RuntimeAPI` as shared mutation foundation

Many cross-cutting flows appear to depend on [RuntimeAPI](/Users/jeff/Developer/vivid/src/runtime/runtime_api.h) rather than mutating runtime state independently. Later review should verify how consistently that remains true.

### 3. Package lifecycle spread across multiple services

The package ecosystem appears deliberately decomposed across manager, compiler, catalog, test runner, and scaffolder files. Later review should examine whether those boundaries are clean in practice or whether responsibility drifts between them.

### 4. Asynchronous background-service boundaries

Hot reload, package catalog refresh, update checks, and capture/recording all involve background or deferred execution. Later review should pay attention to these boundaries because they are process-wide and cross several subsystems.

### 5. Export as a second assembly model

[ExportPipeline](/Users/jeff/Developer/vivid/src/export/export_pipeline.h) appears to recreate part of the runtime assembly story in a standalone-build context. Later review should treat this as a major consistency boundary between live-runtime behavior and exported-binary behavior.

### 6. MCP as tool-shaping rather than truth-owning

The MCP bridges appear intentionally thin over runtime/control services. Later review should examine whether they stay at that layer or whether product logic begins to drift into Python bridge code.

## Open Questions for Phase 6+

These are exploration questions, not findings.

- How complete is the correspondence between `RuntimeAPI` capabilities and `ControlServer` endpoints?
- Which cross-cutting services are covered by tests, and which mainly rely on manual/release verification?
- How much package/update/catalog state is cached on disk, and where are the main persistence assumptions documented?
- How much logic lives in MCP bridge code versus the runtime/control-server side?
- How many control-server endpoints are effectively public contracts that should be reviewed like APIs rather than implementation details?
- How tightly is export coupled to the live operator-registry/build-manifest model?

## Phase Boundary

This phase establishes the broad horizontal integration map for Vivid:

- external control and LLM integration are layered over the runtime command surface
- package install/build/test/scaffold/catalog services form a substantial ecosystem slice
- hot reload, updates, settings, capture, and export are process-level systems that intersect several subsystems at once
- many product surfaces converge on a smaller number of shared runtime interfaces rather than standing alone

The next exploration phase should move to the test suite as a first-class subsystem so later review can compare this architecture map against actual automated coverage.
