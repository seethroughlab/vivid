# Phase 6: Tests, Tooling, CI, And Release Infrastructure

Status: proposed

## Purpose

Verify that the codebase has enough automated evidence and release tooling to support a confident
first release.

## User Task

Run the release candidate through local gates, CI expectations, version checks, packaging scripts,
and documented release steps without relying on private maintainer memory.

## Hypothesis

If tooling is release-ready, a maintainer can reproduce the release decision and understand which
parts are verified versus scaffolded.

## Pressure Test

Run or inspect production gates, test labels, sanitizer targets, release scripts, GitHub Actions,
version guard, appcast generation, signing/notarization scaffolding, and documentation.

## Scope

- CMake build targets, local test commands, production gate, test labels, sanitizer builds, release
  scripts, GitHub Actions, version guard, appcast generation, signing/notarization scripts, docs, and
  artifact layout.
- The difference between verified automation and intentionally scaffolded release infrastructure.

Out of scope: provisioning external secrets or runners unless the audit owner has access.

## Audit Procedure

1. Inventory every documented release and test command, then mark it verified, scaffolded, stale, or
   missing.
2. Run local production-gate and focused tests where practical. Record command, build directory,
   result, duration, and notable warnings.
3. Inspect CI workflows for parity with local commands, required secrets, artifact names, and failure
   behavior.
4. Review release scripts for credential checks, partial artifact cleanup, version/tag consistency,
   appcast correctness, and honest failure messages.
5. Map P0/P1 findings from code phases to automated coverage or a documented manual release check.

## Evidence To Collect

- Command inventory with status and output summaries.
- Production-gate report path and status.
- CI workflow matrix with runner, trigger, secrets, artifacts, and current verification status.
- Release script checklist with missing credential and partial-failure behavior.
- Coverage map for critical release risks.

## Deliverables

- Release infrastructure readiness report.
- Required pre-release command list.
- CI/local parity findings.
- Test gap list tied to release risks.

## Acceptance Criteria

- The production gate has a clear pass/fail interpretation and minimum-test guard.
- Critical code paths have tests proportional to release risk.
- CI and local commands are documented and agree.
- Release scripts fail loudly on missing credentials or partial artifacts.
- Scaffolded release infrastructure is clearly labeled until verified.

## Failure Modes

- A green test run silently exercises too little of the product.
- Local and CI release paths use different assumptions.
- Release scripts produce unsigned, incomplete, or mislabeled artifacts.
- Documentation hides known infrastructure gaps.

## Evidence Log

- Pending.

## Open Questions

- Which build directory and configuration define the release candidate?
- Which CI workflows are required to pass before tagging?
- Are signing, notarization, appcast, and updater behavior required for the first public build or
  explicitly deferred?

## Follow-Up Plans

- Link test additions, CI updates, release-runbook fixes, and production-gate changes here.
