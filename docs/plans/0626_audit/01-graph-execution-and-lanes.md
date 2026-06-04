# Audit 01: Graph Execution & Lanes

**Date:** 2026-06-26
**Status:** Planned

## Purpose

Audit the graph compiler and execution model for correctness risks in topology compilation, lane propagation, executor behavior, and cross-cadence state transfer.

## Scope

- `src/runtime/graph/`
- `docs/runtime/graph.md`
- `src/runtime/graph/CLAUDE.md`
- Graph, lane, executor, and integration tests under `tests/graph/`, `tests/lanes/`, `tests/ops/`, and `tests/integration/`
- Demo graphs that exercise lane-heavy or mixed-cadence behavior

## Primary Questions

- [ ] Are graph compilation passes documented, ordered, and enforced consistently?
- [ ] Are lane identity, lane provenance, and lane cardinality preserved across operators and recompiles?
- [ ] Are frame-cadence and audio-cadence execution boundaries explicit and race-safe?
- [ ] Are graph snapshots complete enough for UI, control server, and MCP consumers?
- [ ] Are invalid graph states rejected early with useful diagnostics?
- [ ] Are topology changes, hot reload, and recompilation handled without stale execution state?
- [ ] Do tests cover scalar, multi-lane, GPU-backed lane, and audio-lane cases?

## Subsystem Checklist

- [ ] Trace graph JSON load through `GraphCompiler` to `CompiledGraph`.
- [ ] Review compile-pass invariants and confirm each pass has narrow ownership.
- [ ] Inspect `FrameExecutor` and `AudioExecutor` for shared assumptions.
- [ ] Check lane buffer, lane state, and lane output adapter ownership/lifetime.
- [ ] Verify graph snapshot fields against UI/control/MCP expectations.
- [ ] Review test fixtures for lane alignment, disconnected ports, invalid connections, and graph reloads.
- [ ] Identify oversized or multi-purpose graph files that obscure compiler or executor contracts.

## Audit Checklist

- [ ] Read the relevant subsystem docs and navigation guides.
- [ ] Inspect the main source files and ownership boundaries.
- [ ] Review tests that claim to cover the subsystem.
- [ ] Check docs/code/test contract drift.
- [ ] Identify correctness, robustness, and maintainability findings.
- [ ] Record findings with severity, category, evidence, and recommendation.
- [ ] Propose immediate, near-term, and backlog follow-up work.

## Findings Template

| ID | Severity | Category | Finding | Location |
|----|----------|----------|---------|----------|

## Completion Criteria

- [ ] Findings table is filled in or explicitly marked with no findings.
- [ ] Each finding has evidence, impact, and a recommended next action.
- [ ] Test gaps are listed separately from implementation findings.
- [ ] Docs that need updates are identified by path.
- [ ] Follow-up work is grouped into immediate, near-term, and backlog.
