# Code Audit Phases

Status: proposed

The code audit answers whether the release candidate is technically trustworthy: maintainable,
recoverable, realtime-safe, testable, and honest about unfinished infrastructure.

## Phase Index

1. [Architecture And Ownership Boundaries](phase-01-architecture-and-ownership-boundaries.md)
2. [Realtime Audio And Thread Safety](phase-02-realtime-audio-and-thread-safety.md)
3. [Rendering, UI, And GPU Runtime](phase-03-rendering-ui-and-gpu-runtime.md)
4. [Persistence, Undo, And Project Recovery](phase-04-persistence-undo-and-project-recovery.md)
5. [Packages, Operators, And Plugin Hosting](phase-05-packages-operators-and-plugin-hosting.md)
6. [Tests, Tooling, CI, And Release Infrastructure](phase-06-tests-tooling-ci-and-release-infrastructure.md)

## Shared Code Evidence

- File and line references for each finding.
- Test, sanitizer, or production-gate output for each verified risk.
- Notes on user impact, not just implementation preference.
- Links to UX findings when a code issue affects visible behavior.
- Explicit release decision for every P0/P1 finding.

## Code Audit Method

Run code phases as release-risk reviews. The output is a prioritized map of risk, not a broad style
cleanup backlog.

For every code review pass, capture:

- Entry points inspected.
- Ownership boundary or invariant being checked.
- Tests, scripts, or sanitizer runs that support the claim.
- Exact file references for risky behavior.
- Whether the issue blocks release, needs a targeted test, or should become an ADR.

## Cross-Phase Dependencies

- Phase 1 should identify the ownership boundaries used by all later phases.
- Phase 2 and Phase 3 must agree on realtime/render-thread handoff rules.
- Phase 4 should include state created by packages and plugins from Phase 5.
- Phase 6 should verify that the highest-risk paths from Phases 1-5 are covered by local or CI
  evidence.
