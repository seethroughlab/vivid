# Audit 09: Seed Operators & Shared Libraries

**Date:** 2026-06-26
**Status:** Planned

## Purpose

Audit seed operators and shared operator libraries for consistency, reusable patterns, lane behavior, preset metadata, DSP/editor duplication, and gaps that make operator authoring harder.

## Scope

- `operators/audio/`
- `operators/control/`
- `operators/gpu/`
- `operators/shared/`
- `operators/CLAUDE.md`
- Operator API docs used by seed operators
- Operator, ops, audio, control, GPU, lane, and shared-library tests

## Primary Questions

- [ ] Do seed operators consistently follow the current operator contract and file conventions?
- [ ] Are parameters, semantic tags, ranges, presets, and docs consistent within each domain?
- [ ] Do operators handle scalar and lane inputs predictably?
- [ ] Are shared DSP, sequencer, plugin, movie, and editor libraries factored at the right level?
- [ ] Are domain-specific exceptions intentional and documented?
- [ ] Are large implementation headers or duplicated editor/DSP patterns hiding correctness risks?
- [ ] Do operator tests cover representative behavior rather than only construction/load success?

## Subsystem Checklist

- [ ] Sample operators from audio, control, GPU, and shared-heavy groups.
- [ ] Review factory presets for schema consistency and useful defaults.
- [ ] Check lane-aware operators for cardinality, reset, and per-lane state behavior.
- [ ] Inspect editor-backed operators for duplicated editor state, selection, and serialization logic.
- [ ] Review shared DSP/plugin/movie/sequencer libraries for ownership and domain leakage.
- [ ] Verify tests cover presets, invalid inputs, reset behavior, hot reload, and lane/vectorized cases.
- [ ] Identify candidate reusable helpers that would reduce future operator-authoring friction.

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
- [ ] Findings distinguish per-operator bugs from shared-library design problems.
- [ ] Parameter/preset consistency issues include domain examples.
- [ ] Test gaps identify which operators or operator families need coverage.
- [ ] Follow-up work is grouped into immediate, near-term, and backlog.
