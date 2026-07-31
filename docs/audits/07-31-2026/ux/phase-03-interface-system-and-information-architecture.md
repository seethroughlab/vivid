# Phase 3: Interface System And Information Architecture

Status: proposed

## Purpose

Verify that the app follows the Vivid UI principles: strict domain zones, focus-first editing,
content-forward presentation, and shared style primitives.

## User Task

Navigate between session, audio graph, visual graph, inspectors, popups, previews, and diagnostics
without losing domain context or focus.

## Hypothesis

If the interface system is coherent, new feature density will not feel like accidental clutter and
existing controls will be predictable.

## Pressure Test

Audit all visible panels, popups, menus, inspectors, graph views, preview surfaces, and diagnostics
against `docs/product/ui-principles.md`.

## Scope

- Persistent regions, deep views, inspectors, graph canvases, output preview, popups, menus,
  diagnostics, and any release-visible toolbars.
- Domain identity for audio, visual, bridge, and shared surfaces.
- Layout behavior at normal desktop, narrow desktop, and minimum supported window sizes.
- Draw/hit-test consistency as experienced by the user.

Out of scope: redesigning the interface language unless the audit finds release-blocking conflict
with accepted principles.

## Audit Procedure

1. Create a screen inventory of every release-visible view and state.
2. Classify each region as persistent zone, deep view, inspector, popup/menu, preview, or diagnostic
   surface.
3. Check each region against the UI principles: strict domain zones, one focused editor, hard-edged
   style, content-forward layout, and shared geometry.
4. Resize the window through representative sizes and record text clipping, overlap, lost controls,
   or hit target drift.
5. Verify that selection, focus, hover, disabled, warning, and error states are visually distinct.

## Evidence To Collect

- Annotated screenshots for each primary state.
- Region inventory table with domain, role, source file if known, and principle violations.
- Layout stress notes for constrained windows.
- Screenshots of any overlap, stale focus, or ambiguous selection state.

## Deliverables

- Information architecture map of release-visible regions.
- UI principle compliance matrix.
- Prioritized layout and visual-system findings.

## Acceptance Criteria

- Audio, visual, bridge, and shared regions are visually and spatially distinguishable.
- Selection and focus are represented consistently.
- Rich editors are not squeezed into inspector rows.
- Hit targets, layout bounds, and text remain stable across normal window sizes.
- New UI uses shared renderer/style/layout conventions rather than one-off styling.

## Failure Modes

- Multiple domains compete inside one rectangle.
- A view changes meaning based only on hidden selection state.
- Controls overlap, resize unexpectedly, or lose labels in constrained layouts.
- One-off UI drawing creates visual exceptions users must relearn.

## Evidence Log

- Pending.

## Open Questions

- What is the minimum supported app window size for the first release?
- Which diagnostics are user-facing and which are development-only?
- Are floatable editors part of the release bar or a deferred architectural promise?

## Follow-Up Plans

- Link screenshots, UI principle amendments, or renderer/layout cleanup plans here.
