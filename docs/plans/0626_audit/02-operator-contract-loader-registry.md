# Audit 02: Operator Contract, Loader & Registry

**Date:** 2026-06-26
**Status:** Planned

## Purpose

Audit the public operator contract and runtime operator lifecycle for authoring friction, descriptor correctness, stale artifact detection, and registry/loader consistency.

## Scope

- `src/operator_api/`
- `src/runtime/operators/`
- `docs/runtime/operator_loader.md`
- `docs/OPERATOR-LOADING.md`
- `src/operator_api/CLAUDE.md`
- Operator loader, registry, descriptor, source-doc, and creation tests
- Representative seed operators used as contract examples

## Primary Questions

- [ ] Is the public operator API minimal, clear, and stable enough for generated operators?
- [ ] Do runtime headers leak implementation details that operators should not depend on?
- [ ] Are descriptors validated consistently before runtime use?
- [ ] Does `operator_codegen` own the `extern "C"` boundary without duplicate manual registration paths?
- [ ] Does stale artifact detection catch realistic mismatch cases without pretending to guarantee binary compatibility?
- [ ] Are operator type lookup, aliasing, metadata, and diagnostics consistent across seed and package operators?
- [ ] Are authoring errors reported in a way an LLM or user can act on?

## Subsystem Checklist

- [ ] Review `operator.h`, domain mixins, port/type headers, and editor helper headers for public surface creep.
- [ ] Trace dylib load/probe/registration from file discovery to registry lookup.
- [ ] Inspect descriptor validation and hash/staleness checks for clear failure modes.
- [ ] Compare source-derived docs with runtime operator metadata and MCP `operator_docs`.
- [ ] Check operator scaffolding paths for generic names, package placement, and reusable output.
- [ ] Verify tests cover missing symbols, stale ABI, bad descriptors, duplicate types, and package operator precedence.
- [ ] Identify contract docs that are stale or too implicit for LLM-generated operators.

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
- [ ] Public API risks are separated from loader/registry implementation risks.
- [ ] Operator-authoring friction points are captured with concrete examples.
- [ ] Test gaps are mapped to loader, registry, descriptor, and API-contract behavior.
- [ ] Follow-up work is grouped into immediate, near-term, and backlog.
