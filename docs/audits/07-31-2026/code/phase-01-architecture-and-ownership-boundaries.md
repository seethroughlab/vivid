# Phase 1: Architecture And Ownership Boundaries

Status: proposed

## Purpose

Verify that core subsystems have clear ownership, dependency direction, and extension points before
the first release hardens them into public precedent.

## User Task

Maintain or extend one representative feature without crossing unclear boundaries between app,
audio, GPU, UI, persistence, packages, CLI, and platform layers.

## Hypothesis

If architecture boundaries are healthy, release fixes can be made locally without destabilizing
unrelated creative workflows.

## Pressure Test

Review module boundaries, `CLAUDE.md` guidance, build targets, public headers, and cross-subsystem
call paths for representative features.

## Scope

- `app/src/app`, `audio`, `gpu`, `ui`, `packages`, `operator_api`, `cli`, `platform`, `midi`, and
  persistence files.
- Module-level `CLAUDE.md` guidance and architecture docs.
- Public headers, operator-facing APIs, and cross-subsystem data models.
- Build targets and scripts that define release-supported components.

Out of scope: broad refactors and style-only cleanup unless they hide ownership or release risk.

## Audit Procedure

1. Draw a subsystem dependency map from source directories, public headers, and build files.
2. Pick three representative flows: project load/playback, visual graph edit/render, and
   package/operator load. Trace ownership through UI, runtime, persistence, and control APIs.
3. Identify duplicated state, bidirectional dependencies, platform leakage, and internals exposed
   through public headers.
4. Compare actual boundaries with `docs/decisions/ADR-0025-cpp17-organization-and-patterns.md`
   and relevant module guidance.
5. Mark each boundary concern as release blocker, follow-up cleanup, or ADR candidate.

## Evidence To Collect

- Dependency map or notes listing allowed and suspicious dependency directions.
- File references for cross-boundary state mutation or public API leaks.
- Three flow traces with entry points and ownership handoffs.
- ADR candidates for decisions that should become durable release policy.

## Deliverables

- Architecture boundary report.
- P0/P1 ownership risks with smallest acceptable fixes.
- ADR/follow-up list for non-blocking structural debt.

## Acceptance Criteria

- Each subsystem has a clear owner role and reason to change.
- Shared data models do not require duplicated truth across UI, persistence, and runtime.
- Platform-specific code is isolated behind platform seams.
- Public APIs and operator-facing types are stable enough for first-release documentation.
- Architectural exceptions are documented as intentional debt or ADR candidates.

## Failure Modes

- UI, runtime, and persistence each maintain incompatible state.
- Platform-specific behavior leaks into portable logic.
- A small feature requires edits across many unrelated modules.
- Public headers expose internals that cannot be supported after release.

## Evidence Log

- Pending.

## Open Questions

- Which headers are public operator API versus internal app API?
- Which subsystem owns the canonical project model at runtime?
- What architectural debt is acceptable for first release because it is not yet public precedent?

## Follow-Up Plans

- Link ADR candidates, boundary cleanup plans, or API documentation updates here.
