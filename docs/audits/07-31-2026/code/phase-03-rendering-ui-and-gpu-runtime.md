# Phase 3: Rendering, UI, And GPU Runtime

Status: proposed

## Purpose

Verify that rendering, UI drawing, GPU operator execution, previews, and visual graph behavior are
stable enough for first-release creative use.

## User Task

Build and manipulate a visual graph while the UI remains responsive, the preview reflects state, and
GPU failures stay contained.

## Hypothesis

If rendering boundaries are healthy, users can iterate visually without crashes, blank output, or
state divergence between graph, preview, and saved project.

## Pressure Test

Audit render loop ownership, GPU context lifetime, shader/operator loading, visual graph topology,
UI layout/draw/hit-test coupling, output preview behavior, and diagnostics.

## Scope

- Frame loop, GPU context, shader/operator loading, visual graph runtime, render targets, output
  preview, UI renderer, layout primitives, hit testing, diagnostics, and performance stats.
- GPU and UI failure reporting through both visible UI and control APIs.
- Interaction between visual graph state, persisted project state, and preview output.

Out of scope: aesthetic redesign, shader art direction, or non-release experimental visuals.

## Audit Procedure

1. Trace one frame from app tick through UI draw, visual graph render, GPU presentation, and
   diagnostics.
2. Trace shader/operator load success and failure, including resource lifetime and visible errors.
3. Compare layout source, drawing, and hit testing for each primary UI surface.
4. Exercise visual graph edits while playback or preview is active and inspect state consistency.
5. Review diagnostics for blank output, shader failure, GPU device loss, and frame-time spikes.

## Evidence To Collect

- Frame/render path trace with ownership notes.
- GPU resource lifetime notes for textures, render targets, shaders, and operators.
- UI geometry inventory: layout source, draw function, hit-test function, and drift risks.
- Screenshots or logs for shader failure and blank-output scenarios.

## Deliverables

- Rendering and UI runtime risk report.
- Layout/draw/hit-test mismatch list.
- GPU failure containment and diagnostics recommendations.

## Acceptance Criteria

- Draw and hit-test geometry share the same layout source.
- GPU resources have clear lifetimes and failure paths.
- Shader/operator errors surface in UI and control APIs.
- Visual graph state, preview output, and persisted project state agree.
- Performance diagnostics identify frame-time or GPU failure causes.

## Failure Modes

- Rendering errors become silent blank previews.
- UI layout and hit tests drift apart.
- GPU resource lifetime depends on incidental object order.
- Visual graph edits invalidate preview or persistence state differently.

## Evidence Log

- Pending.

## Open Questions

- What visual output correctness checks are required for release examples?
- How should the app distinguish no-output-by-design from render failure?
- Which GPU errors should trigger quarantine, toast, diagnostics, or project recovery?

## Follow-Up Plans

- Link GPU crash fixes, UI geometry cleanup, diagnostics work, or shader-loader decisions here.
