# Audit 07: Package, Build, Export & Dependencies

**Date:** 2026-06-26
**Status:** Planned

## Purpose

Audit the package lifecycle, CMake build structure, standalone export pipeline, and dependency management for reproducibility, boundary clarity, and developer-loop reliability.

## Scope

- `src/runtime/packages/`
- `cmake/`
- `CMakeLists.txt`
- `src/export/`
- `docs/runtime/package_system.md`
- `docs/ARCHITECTURE.md` dependency manifest
- Package, export, CLI, and build-related tests

## Primary Questions

- [ ] Are package install, link, rebuild, uninstall, and dependency resolution semantics clear?
- [ ] Are build targets modular enough for app, operators, tests, packages, and export?
- [ ] Are dependency versions and platform frameworks declared in one trustworthy place?
- [ ] Does standalone export preserve graph/operator behavior without hidden runtime assumptions?
- [ ] Are package compilation failures reported with actionable diagnostics?
- [ ] Are test partitions aligned with actual resource requirements?
- [ ] Are generated or copied build artifacts isolated from source-tracked state?

## Subsystem Checklist

- [ ] Trace package manifest parsing through install/link/rebuild/uninstall.
- [ ] Review `add_vivid_operator()` and package compilation behavior for seed and package operators.
- [ ] Inspect test CMake partitions for duplication, stale dependencies, and resource labels.
- [ ] Check export pipeline assumptions about graph assets, operator sources, and static linking.
- [ ] Compare dependency declarations with docs and platform assumptions.
- [ ] Verify tests cover broken manifests, missing package dependencies, compile failures, and export fixture behavior.
- [ ] Identify build files that are oversized or own too many concerns.

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
- [ ] Package-manager findings are separated from CMake/export/dependency findings.
- [ ] Reproducibility and artifact-location risks are explicitly checked.
- [ ] Test partition gaps include commands needed to reproduce failures.
- [ ] Follow-up work is grouped into immediate, near-term, and backlog.
