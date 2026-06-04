# Audit 08: UI, Inspector & Interaction

**Date:** 2026-06-26
**Status:** Planned

## Purpose

Audit the retained UI, node graph editor, inspector, dialogs, rendering helpers, and input model for state synchronization, interaction correctness, layout stability, and maintainability risks.

## Scope

- `src/ui/`
- `docs/INTERFACE.md`
- `src/ui/graph/CLAUDE.md`
- UI-related command sink and graph snapshot boundaries
- UI, screenshot, graph, and integration tests that exercise editor behavior

## Primary Questions

- [ ] Is retained UI state synchronized correctly with runtime graph snapshots?
- [ ] Are input state transitions clear for selection, dragging, connecting, editing, and dialogs?
- [ ] Does inspector behavior match operator metadata, parameter lanes, presets, and editor widgets?
- [ ] Are rendering and layout dimensions stable under long labels, small viewports, and dynamic values?
- [ ] Are UI-to-runtime boundaries clean, or do runtime headers depend on UI-only types?
- [ ] Are node graph files still too large or multi-purpose to audit safely?
- [ ] Are screenshot and interaction tests strong enough to catch regressions?

## Subsystem Checklist

- [ ] Trace graph snapshot ingestion into node graph state and rendered output.
- [ ] Review click, drag, hover, text editing, connection, and context-menu state machines.
- [ ] Inspect inspector sections, parameter widgets, operator editors, and dialog interactions.
- [ ] Check rendering helpers for text clipping, layout jitter, and inconsistent theme usage.
- [ ] Review command sink boundaries and shared data types between UI and runtime.
- [ ] Verify tests cover live graph updates, inspector editing, modal input capture, and screenshot baselines.
- [ ] Identify UI files that should be split by interaction state, drawing responsibility, or inspector section.

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
- [ ] Interaction bugs include reproduction steps where possible.
- [ ] Layout/rendering findings specify the viewport or UI state involved.
- [ ] Runtime/UI boundary findings identify the correct destination layer.
- [ ] Follow-up work is grouped into immediate, near-term, and backlog.
