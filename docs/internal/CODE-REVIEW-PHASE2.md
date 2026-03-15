# Code Review Phase 2: Runtime Spine Exploration

## Purpose

This note is the Phase 2 runtime-spine artifact for the Vivid code review process described in [CODE_REVIEW.md](/Users/jeff/Developer/vivid/docs/CODE_REVIEW.md).

The goal of this phase is to trace the core runtime path end to end so later review work has a clear model of orchestration, ownership, and subsystem boundaries. This is still exploration rather than audit. It records:

- the bootstrap path through the runtime
- the central orchestration modules in `src/runtime`
- which runtime subsystems appear to own which kinds of state
- the main contracts that connect runtime subsystems together
- architectural choke points that later review should focus on

This note does not judge correctness, performance, or implementation quality.

## Runtime Overview

At a high level, the runtime spine appears to be organized around one main assembly path in [main.cpp](/Users/jeff/Developer/vivid/src/runtime/main.cpp), plus a small set of long-lived runtime services:

- graph representation and persistence in [graph.h](/Users/jeff/Developer/vivid/src/runtime/graph.h)
- control/GPU scheduling in [scheduler.h](/Users/jeff/Developer/vivid/src/runtime/scheduler.h)
- audio-domain execution in [audio_engine.h](/Users/jeff/Developer/vivid/src/runtime/audio_engine.h)
- operator discovery/loading in [operator_registry.h](/Users/jeff/Developer/vivid/src/runtime/operator_registry.h) and [operator_loader.h](/Users/jeff/Developer/vivid/src/runtime/operator_loader.h)
- live graph mutation/persistence commands in [runtime_api.h](/Users/jeff/Developer/vivid/src/runtime/runtime_api.h)
- external control and inspection in [control_server.h](/Users/jeff/Developer/vivid/src/runtime/control_server.h)
- package/discovery/build services in [package_manager.h](/Users/jeff/Developer/vivid/src/runtime/package_manager.h) and related files

The runtime appears to treat the graph, scheduler, and registry as the central shared core. Most other runtime services either build those objects, feed them, inspect them, or rebuild them.

## Bootstrap Path

The current bootstrap sequence in [main.cpp](/Users/jeff/Developer/vivid/src/runtime/main.cpp) appears to be:

1. initialize platform/window/GPU/thumbnail infrastructure
2. construct `OperatorRegistry`
3. probe operator dylibs with deferred scan
4. register built-in operators and WGSL presets
5. load factory presets
6. construct package services and scan installed packages into the registry
7. construct package catalog and app-update services
8. resolve workspace/resource/example graph roots
9. load the graph into `Graph`
10. register graph-defined user filters into the registry
11. load only the operators actually needed by the graph
12. build `Scheduler`
13. allocate GPU textures if GPU operators are present
14. build and start `AudioEngine` if audio operators are present
15. start MIDI listener
16. construct `RuntimeAPI`
17. start `ControlServer`
18. construct UI command sinks/editor surfaces
19. enter the main loop and coordinate per-frame ticking, request processing, capture, and UI

This makes [main.cpp](/Users/jeff/Developer/vivid/src/runtime/main.cpp) the primary runtime assembly root. It appears to be the place where almost all long-lived services are wired together.

## Runtime Dependency Map

### Core execution objects

- [Graph](/Users/jeff/Developer/vivid/src/runtime/graph.h)
  Owns serialized graph state: nodes, connections, params, string params, presets, variations, viewport, and load/save metadata.
- [OperatorRegistry](/Users/jeff/Developer/vivid/src/runtime/operator_registry.h)
  Owns available operator types, deferred probe metadata, WGSL presets, builtin registrations, aliases, and package provenance.
- [Scheduler](/Users/jeff/Developer/vivid/src/runtime/scheduler.h)
  Builds executable control/GPU node state from `Graph + OperatorRegistry` and owns live node instances, wires, per-node port buffers, and GPU-side node resources.
- [AudioEngine](/Users/jeff/Developer/vivid/src/runtime/audio_engine.h)
  Builds a separate audio-domain execution structure from `Graph + OperatorRegistry + Scheduler`, then owns the real-time audio callback path, audio node state, cross-domain snapshots, and audio analysis return path.
- [RuntimeAPI](/Users/jeff/Developer/vivid/src/runtime/runtime_api.h)
  Mutates the live runtime and persisted graph model together. Appears to be the primary command surface for in-process graph editing.

### Supporting runtime services

- [ControlServer](/Users/jeff/Developer/vivid/src/runtime/control_server.h)
  HTTP/MCP-facing wrapper around `RuntimeAPI`, graph/scheduler/registry inspection, capture, packages, updates, and diagnostics.
- [PackageManager](/Users/jeff/Developer/vivid/src/runtime/package_manager.h)
  Resolves package scopes, installs/links/rebuilds packages, and scans installed packages into the registry.
- [PackageCompiler](/Users/jeff/Developer/vivid/src/runtime/package_compiler.h)
  Build helper invoked by package manager and rebuild flows.
- [PackageCatalog](/Users/jeff/Developer/vivid/src/runtime/package_catalog.h)
  Remote catalog/update metadata source layered on package manager state.
- [AppUpdateManager](/Users/jeff/Developer/vivid/src/runtime/app_update_manager.h)
  Core-app update metadata fetcher.
- [CaptureCoordinator](/Users/jeff/Developer/vivid/src/runtime/capture_coordinator.h)
  Main-thread capture/recording orchestration across GPU and audio.
- [Settings](/Users/jeff/Developer/vivid/src/runtime/settings.h)
  Process-level persisted settings for UI/editor/update/workspace/operator-destination behavior.

## Central Runtime Contracts

### 1. Graph contract

**Primary files**
- [graph.h](/Users/jeff/Developer/vivid/src/runtime/graph.h)
- [graph.cpp](/Users/jeff/Developer/vivid/src/runtime/graph.cpp)

**What it carries**
- node identity and type
- numeric and string params
- connections and remap metadata
- graph-level filters, MIDI mappings, variations, presets, and viewport metadata
- source path and schema/version metadata
- package provenance and load diagnostics

**Role in the runtime spine**
- persistent source of truth for the patch
- rebuilt into scheduler/audio execution structures
- mutated through `RuntimeAPI`
- serialized back to disk by runtime/UI actions

### 2. Operator descriptor / loader / registry contract

**Primary files**
- [operator_loader.h](/Users/jeff/Developer/vivid/src/runtime/operator_loader.h)
- [operator_registry.h](/Users/jeff/Developer/vivid/src/runtime/operator_registry.h)
- [src/operator_api/types.h](/Users/jeff/Developer/vivid/src/operator_api/types.h)

**What it carries**
- operator descriptors and process entry points
- deferred probe metadata for UI/catalog lookup without eager `dlopen`
- builtin operator registration
- user/WGSL filter registration
- package provenance and alias mappings

**Role in the runtime spine**
- mediates between graph type names and executable operator instances
- feeds scheduler build, audio build, type listing, and introspection
- appears to be the single availability/metadata source for operator types

### 3. Scheduler contract

**Primary files**
- [scheduler.h](/Users/jeff/Developer/vivid/src/runtime/scheduler.h)
- [scheduler.cpp](/Users/jeff/Developer/vivid/src/runtime/scheduler.cpp)

**What it carries**
- live control/GPU node instances
- unified node and wire structures
- port indices, param indices, file-param storage, spread buffers, custom port buffers
- per-node GPU resources and sink detection
- missing-operator placeholder state
- hot-reload/reinit hooks

**Role in the runtime spine**
- converts graph structure into executable control/GPU topology
- appears to own the main-thread live node state
- acts as the source of truth for UI/runtime inspection of live nodes
- provides the control-side state that the audio engine snapshots from

### 4. Audio bridge contract

**Primary files**
- [audio_engine.h](/Users/jeff/Developer/vivid/src/runtime/audio_engine.h)
- [audio_engine.cpp](/Users/jeff/Developer/vivid/src/runtime/audio_engine.cpp)

**What it carries**
- a separate audio-node graph extracted from the main graph
- audio-only wires and specialized cross-domain wire types
- parameter, spread, string, and custom-input snapshot structures
- analysis snapshots flowing back to scheduler/control
- recording tap state and miniaudio device lifecycle

**Role in the runtime spine**
- owns real-time audio processing and audio-thread-safe state
- consumes scheduler state via snapshot push/update calls
- writes analysis back into the control/runtime side through `inject_analysis`

### 5. Runtime command contract

**Primary files**
- [runtime_api.h](/Users/jeff/Developer/vivid/src/runtime/runtime_api.h)
- [control_server.h](/Users/jeff/Developer/vivid/src/runtime/control_server.h)

**What it carries**
- live param/string/layout/resolution updates
- buffered topology changes and rebuild application
- variation/preset/state-preset commands
- save/reload/new-graph/new-project flows
- solo mode and dirty-state tracking
- external HTTP/MCP command handling and introspection/diagnostics

**Role in the runtime spine**
- `RuntimeAPI` appears to be the authoritative mutation surface for the running graph
- `ControlServer` appears to be an adapter layer that translates HTTP/MCP requests into runtime/package/capture/update actions

## State Ownership Summary

### Graph owns persisted model state

[Graph](/Users/jeff/Developer/vivid/src/runtime/graph.h) appears to own the persisted patch model and saveable metadata. This includes data that may not be fully realized in the live scheduler at all times, such as presets, variations, filters, and viewport state.

### Scheduler owns live main-thread node execution state

[Scheduler](/Users/jeff/Developer/vivid/src/runtime/scheduler.h) appears to own instantiated control/GPU operators, per-node buffers, resolved port indices, current param values, runtime string/file values, custom outputs, and per-node GPU resources.

### AudioEngine owns audio-thread execution state

[AudioEngine](/Users/jeff/Developer/vivid/src/runtime/audio_engine.h) appears to own a second execution graph specialized for audio-domain operators, plus the bridging snapshots needed to move data between the scheduler/control side and the audio callback side.

### OperatorRegistry owns type availability and metadata

[OperatorRegistry](/Users/jeff/Developer/vivid/src/runtime/operator_registry.h) appears to own the catalog of known operator types, including deferred metadata, builtins, WGSL presets, aliases, package provenance, and factory presets.

### RuntimeAPI owns mutation coordination

[RuntimeAPI](/Users/jeff/Developer/vivid/src/runtime/runtime_api.h) appears to be the main coordinator that keeps `Graph`, `Scheduler`, and `AudioEngine` in sync when the live graph changes. It also owns graph dirty tracking, quantized variation switching, active preset bookkeeping, and some path-resolution behavior for string/file params.

### main.cpp owns process assembly and service wiring

[main.cpp](/Users/jeff/Developer/vivid/src/runtime/main.cpp) appears to be the top-level integration root that wires together:

- platform/window/GPU setup
- registry and builtins
- packages, catalog, updates
- graph load and example discovery
- scheduler/audio engine construction
- runtime API and control server
- capture and editor command sinks
- UI bootstrap and main loop

## Architectural Choke Points

These are not findings. They are the runtime areas that later review is most likely to hinge on.

### 1. `main.cpp` as integration root

[main.cpp](/Users/jeff/Developer/vivid/src/runtime/main.cpp) appears to contain both startup assembly and substantial runtime coordination logic. Later review should treat it as the top-level integration map and likely coupling hotspot.

### 2. `Graph -> Scheduler -> AudioEngine` rebuild boundary

The runtime appears to rebuild execution state by flowing persisted graph state into scheduler state and then audio state. Later review should focus on how consistent this boundary is across reloads, graph edits, and package/operator changes.

### 3. `RuntimeAPI` as mutation coordinator

[RuntimeAPI](/Users/jeff/Developer/vivid/src/runtime/runtime_api.h) appears to be where persisted graph state and live scheduler state are intentionally kept aligned. Later review should center on this file when asking how graph edits actually propagate.

### 4. `OperatorRegistry` / `OperatorLoader` as plugin boundary

The loader/registry pair is the runtime's operator boundary: deferred probing, ABI checks, load-for-graph, builtins, WGSL filters, package operators, and custom type registration all pass through it.

### 5. `ControlServer` as external surface aggregator

[ControlServer](/Users/jeff/Developer/vivid/src/runtime/control_server.h) appears to aggregate runtime mutation, graph inspection, diagnostics, capture, package actions, and update checks into one external service boundary.

### 6. Cross-domain data movement in `AudioEngine`

[audio_engine.h](/Users/jeff/Developer/vivid/src/runtime/audio_engine.h) appears to contain a large amount of explicit bridging machinery for params, spreads, strings, custom types, analysis snapshots, and audio-node extraction. Later review should treat this as the core cross-domain execution boundary.

## Runtime Subsystem Inventory

### Bootstrap / Process Assembly

**Primary file**
- [main.cpp](/Users/jeff/Developer/vivid/src/runtime/main.cpp)

**What it appears to own**
- application startup and shutdown
- wiring of GPU, registry, packages, graph load, scheduler, audio, runtime API, control server, capture, and UI
- CLI-mode entrypoints alongside interactive app startup

**Classification**
- central orchestration root

### Graph + Persistence

**Primary files**
- [graph.h](/Users/jeff/Developer/vivid/src/runtime/graph.h)
- [graph.cpp](/Users/jeff/Developer/vivid/src/runtime/graph.cpp)

**What it appears to own**
- patch serialization model
- save/load/mutation helpers
- graph-level metadata beyond simple node/connection state

**Classification**
- central data model

### Control / GPU Execution

**Primary files**
- [scheduler.h](/Users/jeff/Developer/vivid/src/runtime/scheduler.h)
- [scheduler.cpp](/Users/jeff/Developer/vivid/src/runtime/scheduler.cpp)

**What it appears to own**
- live main-thread operator instances
- wire routing and port staging
- generation-based cooking state
- per-node GPU resource ownership
- missing-operator placeholders and reload hooks

**Classification**
- central execution core

### Audio Execution

**Primary files**
- [audio_engine.h](/Users/jeff/Developer/vivid/src/runtime/audio_engine.h)
- [audio_engine.cpp](/Users/jeff/Developer/vivid/src/runtime/audio_engine.cpp)

**What it appears to own**
- audio graph extraction/build
- real-time callback processing
- control-to-audio and audio-to-control bridging
- analysis and recording tap state

**Classification**
- central execution core

### Operator Discovery / Loading

**Primary files**
- [operator_loader.h](/Users/jeff/Developer/vivid/src/runtime/operator_loader.h)
- [operator_registry.h](/Users/jeff/Developer/vivid/src/runtime/operator_registry.h)

**What it appears to own**
- plugin ABI boundary
- deferred probing and lazy load
- builtin and WGSL registration
- package provenance and type catalog behavior

**Classification**
- central runtime boundary

### Runtime Command Surface

**Primary files**
- [runtime_api.h](/Users/jeff/Developer/vivid/src/runtime/runtime_api.h)
- [control_server.h](/Users/jeff/Developer/vivid/src/runtime/control_server.h)

**What it appears to own**
- in-process graph mutation coordination
- HTTP/MCP-facing command handling and inspection
- dirty-state and save/reload/new-project behavior

**Classification**
- central operational boundary

### Package / Discovery / Update Services

**Primary files**
- [package_manager.h](/Users/jeff/Developer/vivid/src/runtime/package_manager.h)
- [package_compiler.h](/Users/jeff/Developer/vivid/src/runtime/package_compiler.h)
- [package_catalog.h](/Users/jeff/Developer/vivid/src/runtime/package_catalog.h)
- [app_update_manager.h](/Users/jeff/Developer/vivid/src/runtime/app_update_manager.h)

**What they appear to own**
- package discovery across scopes
- install/link/rebuild/uninstall behavior
- package update classification
- remote catalog and app-update metadata

**Classification**
- important supporting services, attached to the core runtime rather than part of the frame loop itself

### Settings / Shared Handles / Capture / Export

**Primary files**
- [settings.h](/Users/jeff/Developer/vivid/src/runtime/settings.h)
- [shared_handle_registry.h](/Users/jeff/Developer/vivid/src/runtime/shared_handle_registry.h)
- [capture_coordinator.h](/Users/jeff/Developer/vivid/src/runtime/capture_coordinator.h)
- [export_pipeline.h](/Users/jeff/Developer/vivid/src/export/export_pipeline.h)

**What they appear to own**
- persisted process settings
- generic shared opaque handle registry for cross-domain/runtime-backed resources
- frame/audio/recording capture orchestration
- standalone export generation

**Classification**
- supporting horizontal services

## Open Questions for Phase 3+

1. Which parts of `Scheduler` are the canonical pattern for new operator/runtime interactions, and which parts are special handling for complex domains like GPU textures or custom ports?
2. How much duplicated state exists between `Graph`, `Scheduler`, and `AudioEngine`, and which layer is treated as authoritative during rebuilds and reloads?
3. How much of `main.cpp` is pure bootstrap versus long-term runtime coordination that might belong to named subsystems?
4. Which runtime services are tightly coupled to the UI/editor flow versus reusable in headless/export/control-server contexts?
5. Which runtime contracts are stable public-facing architecture, and which are still evolving implementation details?

## Phase Boundary

Phase 2 is complete when:

- the runtime bootstrap path is clear
- the core execution objects and their boundaries are identified
- major runtime-owned state has an owner map
- the later audit has a shortlist of runtime choke points to review deeply

This note stays at the orchestration level. Detailed correctness, race/lifetime, performance, and behavioral review belong to later phases.
