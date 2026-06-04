# Audit 03: Runtime Core & Hot Reload

**Date:** 2026-06-26
**Status:** Planned

## Purpose

Audit the application lifecycle, `RuntimeCore` orchestration, hot reload, crash recovery, settings, undo, and file-watching paths for state ownership and reliability risks.

## Scope

- `src/runtime/core/`
- `docs/runtime/architecture.md`
- `docs/runtime/runtime_core.md`
- Runtime core tests under `tests/core/`
- Integration tests that exercise reload, settings, undo, file watching, crash recovery, or startup/shutdown behavior

## Primary Questions

- [ ] Are startup, shutdown, and runtime reinitialization paths explicit and testable?
- [ ] Does `RuntimeCore` own the right state, or is responsibility spread across main-loop helpers?
- [ ] Can hot reload leave stale graph, operator, UI, or audio state behind?
- [ ] Are crash recovery and safe mode reliable after partial initialization failures?
- [ ] Are settings, workspace, source index, and file watcher interactions well bounded?
- [ ] Are undo and command routing consistent with graph mutation semantics?
- [ ] Are large core files still hiding separable lifecycle concerns?

## Subsystem Checklist

- [ ] Trace app bootstrap from process entry to ready runtime.
- [ ] Review `RuntimeCore` interactions with graph compilation, operator registry, UI, audio, and control surfaces.
- [ ] Inspect hot reload and file watcher behavior for duplicate events, missed rebuilds, and stale pointers.
- [ ] Check crash guard, recovery, quarantine, and safe-mode behavior around failed operators.
- [ ] Review settings/workspace/source-index persistence and reload semantics.
- [ ] Verify undo tests cover grouped mutations, reload boundaries, and failed commands.
- [ ] Identify lifecycle behavior that is only covered through manual UI usage.

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
- [ ] Lifecycle, hot reload, crash recovery, and undo findings are distinguished.
- [ ] Each state ownership concern identifies the current owner and preferred owner.
- [ ] Test gaps include realistic reload and failure scenarios.
- [ ] Follow-up work is grouped into immediate, near-term, and backlog.
