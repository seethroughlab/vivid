# Code Review Phase 1: Product + Architecture Orientation

## Purpose

This note is the Phase 1 orientation artifact for the Vivid code review process described in [CODE_REVIEW.md](/Users/jeff/Developer/vivid/docs/internal/CODE_REVIEW.md).

The purpose of this phase is to build a reliable top-level map of the current `vivid` repo before any real audit begins. This document does not make quality judgments, does not propose fixes, and does not rank risks. It records:

- what Vivid is trying to be
- what major subsystems exist now
- where those subsystems live in the repo
- which docs appear to describe the current system
- which areas look like likely doc/code drift to revisit later

Main repo only. Sibling repos are deferred to later phases.

## Current Product Model

Based on the current top-level docs, Vivid is a macOS-first real-time creative coding environment where audio, visuals, and control live in one graph and where the environment itself is the product.

The current product model emphasizes:

- a unified graph for control, audio, and GPU behavior
- rapid experimentation through parameter editing, graph rewiring, hot reload, and operator authoring
- a minimal but extensible seed operator set
- package-based extension and project-local operator growth
- LLM-assisted workflows through the control server and MCP bridge
- a polished runtime/editor environment rather than a large built-in operator library

The main high-level docs currently shaping that model are:

- [PRD.md](/Users/jeff/Developer/vivid/docs/PRD.md)
- [ARCHITECTURE.md](/Users/jeff/Developer/vivid/docs/ARCHITECTURE.md)
- [INTERFACE.md](/Users/jeff/Developer/vivid/docs/INTERFACE.md)
- [LLM-INTEGRATION.md](/Users/jeff/Developer/vivid/docs/LLM-INTEGRATION.md)
- [ROADMAP.md](/Users/jeff/Developer/vivid/docs/ROADMAP.md)

## Current Codebase Shape

The current repo shape is organized around a small set of central implementation areas:

- [src/runtime](/Users/jeff/Developer/vivid/src/runtime)
  Runtime orchestration, graph/scheduler, package management, control server, settings, platform glue.
- [src/operator_api](/Users/jeff/Developer/vivid/src/operator_api)
  Public operator contract, shared types, domain base classes, custom port/media/session-related contracts.
- [src/ui](/Users/jeff/Developer/vivid/src/ui)
  Retained-mode editor and interaction surfaces, including the node graph and supporting overlays.
- [src/export](/Users/jeff/Developer/vivid/src/export)
  Standalone/export pipeline.
- [operators/control](/Users/jeff/Developer/vivid/operators/control)
  Seed control-domain operators.
- [operators/audio](/Users/jeff/Developer/vivid/operators/audio)
  Seed audio-domain operators.
- [operators/gpu](/Users/jeff/Developer/vivid/operators/gpu)
  Seed GPU-domain operators.
- [operators/shared](/Users/jeff/Developer/vivid/operators/shared)
  Shared movie/media/session internals and other cross-operator support code.
- [tests](/Users/jeff/Developer/vivid/tests)
  Unit and integration tests, graph fixtures, and operator/plugin fixtures.
- [docs](/Users/jeff/Developer/vivid/docs)
  Product, architecture, workflow, roadmap, and internal engineering references.

At this level, the repo appears to be centered on six major areas:

1. runtime/execution core
2. operator contract and seed operators
3. UI/editor
4. package/discovery/distribution surfaces
5. testing/integration surfaces
6. documentation/spec surfaces

## Subsystem Inventory

### Runtime / Execution Core

**Primary location**
- [src/runtime](/Users/jeff/Developer/vivid/src/runtime)

**What it appears to own**
- graph loading and build
- scheduler execution
- operator loading/registry
- audio engine
- package install/link/rebuild/catalog behavior
- control server, settings, update-related runtime concerns

**What kinds of files live here**
- core orchestration modules
- package and runtime services
- platform-specific integration points

**Classification**
- central

### Operator Contract

**Primary location**
- [src/operator_api](/Users/jeff/Developer/vivid/src/operator_api)

**What it appears to own**
- public operator ABI and descriptor types
- domain process contexts
- base classes for control/audio/GPU operators
- custom port/media/session-related shared contracts

**What kinds of files live here**
- headers only, mostly public-facing API and helper abstractions

**Classification**
- central

### Seed Operators

**Primary locations**
- [operators/control](/Users/jeff/Developer/vivid/operators/control)
- [operators/audio](/Users/jeff/Developer/vivid/operators/audio)
- [operators/gpu](/Users/jeff/Developer/vivid/operators/gpu)
- [operators/shared](/Users/jeff/Developer/vivid/operators/shared)

**What they appear to own**
- the shipped seed operator set
- domain examples for operator authoring
- some complex shared internals, especially around movie/media support

**What kinds of files live here**
- one-directory-per-operator source layouts
- shared implementation helpers for related operator families

**Classification**
- central for product behavior, supporting for architecture

### UI / Editor

**Primary location**
- [src/ui](/Users/jeff/Developer/vivid/src/ui)

**What it appears to own**
- retained-mode graph editor
- drawing/layout/input handling
- example/package browsing surfaces
- renderer and thumbnail support

**What kinds of files live here**
- UI orchestration, draw/input split, rendering support, platform dialog/menu integration

**Classification**
- central

### Export

**Primary location**
- [src/export](/Users/jeff/Developer/vivid/src/export)

**What it appears to own**
- standalone export packaging/build flow

**What kinds of files live here**
- export pipeline and standalone main/build template code

**Classification**
- supporting

### Tests

**Primary location**
- [tests](/Users/jeff/Developer/vivid/tests)

**What it appears to own**
- unit and integration coverage for runtime, control server, packages, graphs, and fixtures

**What kinds of files live here**
- executable test sources, operator/plugin fixtures, graph fixtures

**Classification**
- central for later review confidence, but supporting during orientation

### Documentation / Specs

**Primary locations**
- [docs](/Users/jeff/Developer/vivid/docs)
- [docs/internal](/Users/jeff/Developer/vivid/docs/internal)

**What they appear to own**
- product model, architecture intent, interface expectations, roadmap state, and internal engineering references

**What kinds of files live here**
- user-facing and maintainer-facing markdown docs

**Classification**
- central for Phase 1 orientation

## System Map

### 1. Runtime / Execution Core

**Purpose**
- Execute the graph, coordinate domains, manage operators, packages, runtime services, and platform integration.

**Principal directory**
- [src/runtime](/Users/jeff/Developer/vivid/src/runtime)

**Main outward dependencies**
- operator API
- UI
- export
- tests

**Main inward dependencies**
- operators depend on the runtime indirectly through runtime-owned process contexts and services

### 2. Operator Contract and Seed Operators

**Purpose**
- Define how operators are authored and loaded, and provide the seed building blocks shipped with Vivid.

**Principal directories**
- [src/operator_api](/Users/jeff/Developer/vivid/src/operator_api)
- [operators](/Users/jeff/Developer/vivid/operators)

**Main outward dependencies**
- runtime consumes descriptors and executes operators
- UI/control surfaces inspect descriptors and ports

**Main inward dependencies**
- authoring, hot reload, package compilation, and testing all depend on this contract

### 3. UI / Editor

**Purpose**
- Present and manipulate the live graph and related exploration tools.

**Principal directory**
- [src/ui](/Users/jeff/Developer/vivid/src/ui)

**Main outward dependencies**
- runtime command and snapshot surfaces

**Main inward dependencies**
- user workflows route through this layer for graph editing and browsing tasks

### 4. Package / Discovery / Distribution Surfaces

**Purpose**
- Support linked and installed packages, catalog/discovery flows, and release/distribution-oriented runtime behavior.

**Principal directories**
- [src/runtime](/Users/jeff/Developer/vivid/src/runtime)
- [docs/release](/Users/jeff/Developer/vivid/docs/release)

**Main outward dependencies**
- control server and CLI surfaces
- docs and release workflow docs

**Main inward dependencies**
- operator loading and build behavior

### 5. Testing / Integration Surfaces

**Purpose**
- Provide executable checks over runtime behavior, package behavior, operator behavior, and graph smoke paths.

**Principal directory**
- [tests](/Users/jeff/Developer/vivid/tests)

**Main outward dependencies**
- runtime, operator API, and seeded operators

**Main inward dependencies**
- later audit phases will rely on these tests as coverage evidence

### 6. Documentation / Spec Surfaces

**Purpose**
- Define product intent, architecture intent, interface expectations, and review/roadmap guidance.

**Principal directories**
- [docs](/Users/jeff/Developer/vivid/docs)
- [docs/internal](/Users/jeff/Developer/vivid/docs/internal)

**Main outward dependencies**
- future review and implementation planning

**Main inward dependencies**
- docs reference nearly every major subsystem conceptually

## Code Map

This section maps high-level concepts from the docs to their current code homes.

| Concept | Current code home |
|---|---|
| Graph model | [src/runtime](/Users/jeff/Developer/vivid/src/runtime) |
| Scheduler / execution order | [src/runtime](/Users/jeff/Developer/vivid/src/runtime) |
| Audio engine | [src/runtime](/Users/jeff/Developer/vivid/src/runtime) |
| GPU/render loop integration | [src/runtime](/Users/jeff/Developer/vivid/src/runtime) plus GPU operators in [operators/gpu](/Users/jeff/Developer/vivid/operators/gpu) |
| Operator ABI / descriptors / process contexts | [src/operator_api](/Users/jeff/Developer/vivid/src/operator_api) |
| Seed control operators | [operators/control](/Users/jeff/Developer/vivid/operators/control) |
| Seed audio operators | [operators/audio](/Users/jeff/Developer/vivid/operators/audio) |
| Seed GPU operators | [operators/gpu](/Users/jeff/Developer/vivid/operators/gpu) |
| Shared movie/media path | [operators/shared](/Users/jeff/Developer/vivid/operators/shared) plus [operators/gpu/movie_loaded](/Users/jeff/Developer/vivid/operators/gpu/movie_loaded), [operators/gpu/movie_video_out](/Users/jeff/Developer/vivid/operators/gpu/movie_video_out), and [operators/audio/movie_audio_out](/Users/jeff/Developer/vivid/operators/audio/movie_audio_out) |
| Node graph UI | [src/ui](/Users/jeff/Developer/vivid/src/ui) |
| Package system | [src/runtime](/Users/jeff/Developer/vivid/src/runtime) |
| Control server / runtime API | [src/runtime](/Users/jeff/Developer/vivid/src/runtime) |
| Export pipeline | [src/export](/Users/jeff/Developer/vivid/src/export) |
| Test infrastructure | [tests](/Users/jeff/Developer/vivid/tests) |
| Product / architecture reference docs | [docs](/Users/jeff/Developer/vivid/docs) |
| Internal design / evaluation docs | [docs/internal](/Users/jeff/Developer/vivid/docs/internal) |

## Drift Candidates

These are doc/code drift candidates to revisit later. They are not findings.

### 1. Port type system description

- **Doc source**
  - [ARCHITECTURE.md](/Users/jeff/Developer/vivid/docs/ARCHITECTURE.md)
- **Likely code locations**
  - [src/operator_api](/Users/jeff/Developer/vivid/src/operator_api)
  - [src/runtime](/Users/jeff/Developer/vivid/src/runtime)
- **Why revisit later**
  - The repo has recent custom-port and media/session-related contract work that may have evolved beyond the exact model described in the top-level architecture doc.

### 2. Operator inventory and examples

- **Doc source**
  - [ARCHITECTURE.md](/Users/jeff/Developer/vivid/docs/ARCHITECTURE.md)
  - [GETTING-STARTED.md](/Users/jeff/Developer/vivid/docs/GETTING-STARTED.md)
- **Likely code locations**
  - [operators](/Users/jeff/Developer/vivid/operators)
- **Why revisit later**
  - The seeded operator set has shifted over time, including movie, string, OSC, and Syphon-related paths.

### 3. Package / discovery / release surfaces

- **Doc source**
  - [ROADMAP.md](/Users/jeff/Developer/vivid/docs/ROADMAP.md)
  - [LLM-INTEGRATION.md](/Users/jeff/Developer/vivid/docs/LLM-INTEGRATION.md)
- **Likely code locations**
  - [src/runtime](/Users/jeff/Developer/vivid/src/runtime)
  - [docs/release](/Users/jeff/Developer/vivid/docs/release)
- **Why revisit later**
  - The roadmap is now compressed and launch-focused, while the codebase contains substantial package, discovery, and update-related runtime surfaces that likely deserve a richer cross-check.

### 4. UI/editor breadth

- **Doc source**
  - [INTERFACE.md](/Users/jeff/Developer/vivid/docs/INTERFACE.md)
- **Likely code locations**
  - [src/ui](/Users/jeff/Developer/vivid/src/ui)
- **Why revisit later**
  - The implemented UI now includes browsers, overlays, and additional editing flows that may not be fully represented in high-level interface docs.

### 5. Media/movie architecture terminology

- **Doc source**
  - [ARCHITECTURE.md](/Users/jeff/Developer/vivid/docs/ARCHITECTURE.md)
  - [PRD.md](/Users/jeff/Developer/vivid/docs/PRD.md)
- **Likely code locations**
  - [operators/shared](/Users/jeff/Developer/vivid/operators/shared)
  - movie-related operators under [operators/audio](/Users/jeff/Developer/vivid/operators/audio) and [operators/gpu](/Users/jeff/Developer/vivid/operators/gpu)
- **Why revisit later**
  - The repo contains a newer `MovieLoaded` / `MovieVideoOut` / `MovieAudioOut` structure that may not match older mental models embedded in docs.

## Open Questions for Phase 2+

These are orientation questions that need deeper later exploration rather than immediate answers.

1. Which files in [src/runtime](/Users/jeff/Developer/vivid/src/runtime) are now the true orchestration spine, versus legacy-adjacent or supporting runtime services?
2. How cleanly separated are UI state and runtime/application state in the current implementation?
3. Which seed operators are now the canonical references for operator authoring in each domain?
4. Which package/discovery/update surfaces are current, and which are leftover from prior roadmap phases?
5. Which high-level docs should be treated as design intent only, versus accurate descriptions of the present system?

## Phase Boundary

Phase 1 is complete when:

- every major top-level implementation area has been classified
- the major doc-level concepts have concrete code homes or are explicitly unresolved
- drift candidates are recorded without turning into findings
- the next reviewer can begin Phase 2 without rediscovering the overall repo structure

This document is intentionally broad. Deeper tracing, ownership validation, and subsystem correctness review belong to later phases.
