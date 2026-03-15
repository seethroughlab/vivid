# Vivid Code Review Preparation

## Purpose

This document describes how to explore the Vivid codebase systematically before doing any actual code audit or review.

The goal of this process is not to find bugs, propose fixes, or judge quality yet. The goal is to build a reliable map of the system after several weeks of organic development so that a later review can be structured, efficient, and complete.

This is an internal engineering guide for future reviewers or agents working on the main `vivid` repo.

## Phased Exploration Plan

### Phase 1: Product + Architecture Orientation

**Objective**

Understand what Vivid is intended to be, how the major subsystems are supposed to fit together, and where those ideas live in the codebase.

**What to read / inspect**

- `docs/PRD.md`
- `docs/ARCHITECTURE.md`
- `docs/INTERFACE.md`
- `docs/LLM-INTEGRATION.md`
- current roadmap summary
- high-level repo structure under `src/`, `operators/`, `tests/`, and `docs/`

**Artifacts / notes to produce**

- a short system map of the intended architecture
- a code map of where those concepts live in the repo
- a list of obvious doc/code drift areas to revisit later

**Defer until the real audit**

- whether the architecture is correct
- whether docs are accurate enough
- whether particular design choices were well executed

### Phase 2: Runtime Spine Exploration

**Objective**

Trace the core runtime path end to end so later review work has a clear model of orchestration, ownership, and subsystem boundaries.

**What to read / inspect**

- `src/runtime/` entrypoints and central orchestration files
- graph load/build flow
- scheduler execution
- audio engine
- GPU/render loop
- runtime API and control server entrypoints

**Artifacts / notes to produce**

- runtime dependency map
- state ownership summary
- list of architectural choke points and central contracts

**Defer until the real audit**

- correctness of execution order
- performance concerns
- race/lifetime bug hunting

### Phase 3: Operator Contract + Seed Operator Exploration

**Objective**

Understand the operator authoring contract first, then identify representative seed operators that define current patterns.

**What to read / inspect**

- `src/operator_api/`
- representative operators from:
  - `operators/control/`
  - `operators/audio/`
  - `operators/gpu/`
  - `operators/shared/`

Prefer representative sampling over reading every operator in full.

**Artifacts / notes to produce**

- operator authoring mental model
- seed-operator taxonomy
- shortlist of reference operators for later deep review

**Defer until the real audit**

- per-operator correctness
- style consistency judgments
- whether individual operators should be refactored

### Phase 4: UI + Interaction Surface Exploration

**Objective**

Understand the UI as its own subsystem: how it is structured, what state it owns, and where it touches application logic.

**What to read / inspect**

- `src/ui/`
- node graph orchestration
- draw/layout/input split
- overlays, browsers, and editors
- command or callback boundaries to runtime code
- platform-specific menu/dialog integration where relevant

**Artifacts / notes to produce**

- UI module map
- UI state vs runtime state summary
- likely future review hotspots around coupling or complexity

**Defer until the real audit**

- UX quality judgments
- bug-finding in hit testing, focus, or event routing
- redesign proposals

### Phase 5: Cross-Cutting Systems Exploration

**Objective**

Map the systems that cut across the whole product and define important external or operational boundaries.

**What to read / inspect**

- package manager / compiler / catalog
- control server and MCP-facing surfaces
- export pipeline
- hot reload
- settings and persistence
- release/update-related runtime surfaces

**Artifacts / notes to produce**

- external surface inventory
- integration-boundary map
- likely risk clusters for the later audit

**Defer until the real audit**

- package behavior validation
- external API correctness
- release-process critique

### Phase 6: Test Suite Exploration

**Objective**

Understand how the project is tested and what categories of behavior already have explicit coverage.

**What to read / inspect**

- `tests/`
- test target definitions in `CMakeLists.txt`
- test fixtures under `tests/operators/` and `tests/graphs/`

Classify tests by intent:

- unit
- runtime integration
- operator contract
- control-server/API behavior
- graph/demo smoke
- package/release behavior

**Artifacts / notes to produce**

- test topology map
- subsystem-to-test coverage matrix
- list of areas that appear lightly covered

**Defer until the real audit**

- whether coverage is sufficient
- whether specific tests are good or bad
- whether missing tests are a defect

### Phase 7: Synthesis Pass

**Objective**

Turn the exploration notes into a review-ready package that defines how the actual code audit should proceed.

**What to read / inspect**

- all prior phase notes
- subsystem maps
- interface inventories
- coverage notes

**Artifacts / notes to produce**

- consolidated subsystem inventory
- dependency and ownership summary
- public contract list
- review hotspot list
- proposed order for the later audit campaign

**Defer until the real audit**

- findings
- severity ranking of issues
- concrete code change proposals

## Working Method

- Go breadth-first before depth-first.
- Keep this stage non-mutating: read, trace, classify, and map; do not fix or refactor.
- Use a consistent note template for each subsystem:
  - purpose
  - important files
  - key types/functions
  - dependencies
  - major contracts/invariants
  - open questions
- Treat docs, code, and tests as separate views of the same system.
- When a subsystem is large, identify the contract-defining files first, then sample representative implementations.
- Record open questions and likely hotspots, but do not turn them into findings yet.

## Deliverables

The exploration pass should produce:

- one short phase note per phase
- one running subsystem inventory
- one interface inventory for public or cross-subsystem contracts
- one review-hotspot list for the future audit
- one synthesis summary that defines the actual review order

These artifacts should be concise and operational. Their purpose is to make the later audit faster and more complete, not to serve as permanent architecture documentation.

## Assumptions and Defaults

- Start with the main `vivid` repo only.
- Treat sibling repos and packages as a later follow-on exploration pass.
- Do not produce findings, fixes, or refactor recommendations during this stage.
- The purpose of this work is to prepare a later structured code review, not to replace it.
- This document is an internal process guide, not user-facing documentation.
