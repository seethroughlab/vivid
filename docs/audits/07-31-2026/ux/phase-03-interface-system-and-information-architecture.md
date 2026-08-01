# Phase 3: Interface System And Information Architecture

Status: done (audited 2026-08-01)

## Verdict

**PASS with follow-ups — no P0/P1.** The interface system holds up well against
`docs/product/ui-principles.md`. **Strict domain zones are real and legible**: the Session View +
per-track audio graph read **amber**, the visual graph reads **cyan**, the mapping bridge reads
**teal**, and diagnostics carries the **severity palette** (green/gold/red) — each zone is
distinguishable by both position and colour (`evidence/phase-03/01-diagnostics-panel.png`,
`03-shader-library.png`, and the Phase-1/2 session/graph/mapping shots). **One focused editor at a
time** holds — the bottom detail region hosts the audio graph *or* the clip editor, never both
(`07-clip-editor.png`). **Rich editors get their own canvas** (the clip editor is a full piano-roll
with a velocity lane and a `float` button, not inspector rows), and the hard-edged, content-forward
style (90° corners, 1px rules, dark steel) is consistent across every surface. Draw/hit-test share
one geometry source — independently confirmed CLEAN in the code audit (Phase 3 §D).

Findings are **3×P3**, all layout/visual-polish: the window has **no enforced minimum size** so it
can be dragged into a broken layout (top bar overlaps, session grid clips —
`06-resize-minimum.png`); two neutral-gray popups (the shader library and the add-node chooser)
**carry no domain-identity colour** despite being visual/context surfaces; and the shader-library
description column **truncates mid-word without an ellipsis**. None block release. At normal desktop
sizes (≥~1000px) the layout is stable (`05-resize-narrow-1000.png`).

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

Method: drove the running release build (commit `92496e2f`, post-#208) with the `neon` example
loaded; toggled every overlay via its keybind (`H` diagnostics · `J` log · `L` shader library ·
`M` mappings · `Tab` chooser), opened the clip editor (double-click a clip), and resized the OS
window through desktop / narrow / minimum. Full-window screenshots under `evidence/phase-03/`;
Phase-1/2 shots cover the session view, both graphs, and the mapping bridge. Audited against
`docs/product/ui-principles.md`. Paths relative to repo root.

### A. Information-architecture map (release-visible regions)

| Region | Domain | Role | Source (`app/src/`) | Notes |
|---|---|---|---|---|
| Session View (tracks×scenes grid, transport, mixer) | audio **amber** | persistent zone | `ui/session_view` | left column, fixed width |
| Per-track audio graph | audio **amber** | deep view (bottom detail region) | `ui/audio_node_graph` | contextual to the selected track |
| Visual graph (operators + edges → Output) | visual **cyan** | persistent zone | `ui/node_graph` | main canvas, pannable |
| Output preview | visual artifact | floating preview | `app/output_preview` + `visual_graph::present_to` | pops out to its own OS window (ADR-0014) |
| Clip editor (piano-roll + velocity lane) | audio | rich editor (bottom detail region, **floatable**) | `ui/clip_editor` | replaces the audio-graph deep view; `float` button |
| Mapping overview ("MAPPINGS") | bridge **teal** | modal/popup | `ui/mapping_overview` | `m`; SOURCE→DEST + POL/AMT/CURVE/LO/HI |
| Diagnostics panel | severity (green/gold/red) | modal/diagnostic | `ui/diagnostics_panel` | `h`; incl. the P2-03 "Output feeding" row |
| Log view | neutral | modal/diagnostic | `ui/diagnostics_panel` (`draw_log_view`) | `j`; leveled entries coloured by level |
| Shader library | **gray** (neutral) | modal / content browser (visual) | `ui/shader_library_view` | `l`; NAME/TIER/SUMMARY + open/fork (→ F2/F3) |
| Add-node chooser | **gray** (neutral) | popup (context: audio *or* visual) | `ui/chooser` | `Tab`; filter + name/op/description (→ F2) |
| Preset popover | gray | popup (params) | `ui/preset_popover` | per-node preset save/load |
| Params dock (inspector) | per-domain | inspector strip | params-only dock | curated params, not a rich editor |

### B. UI-principle compliance matrix

| Principle (`ui-principles.md`) | Verdict | Evidence / gap |
|---|---|---|
| **Strict domain zones** (amber/cyan/teal/gray identity) | **PASS** | persistent zones + mapping + diagnostics all colour- and position-distinct. Gap: neutral-gray popups (shader library, chooser) carry no domain identity (→ F2) |
| **One focused editor at a time** (focus ≠ selection) | **PASS** | the bottom detail region hosts the audio graph *or* the clip editor, never both (evidence 07) |
| **Hard-edged style** (90°, 1px rules, dark steel, no chrome) | **PASS** | consistent across every panel/modal captured |
| **Content-forward** (no decorative chrome) | **PASS** | minimal chrome. Gap: shader-library descriptions truncate mid-word (→ F3) |
| **Shared geometry** (draw == hit-test, `ui/layout.h`) | **PASS** | corroborated by the code audit (Phase 3 §D — CLEAN on all 6 surfaces) |
| **Selection = blue; domain = identity; gold = queued/warn** | **PASS** | blue selection border on the focused track header; gold on the active/queued clip; severity green/gold/red in diagnostics |
| **Rich editors ≠ inspector rows** | **PASS** | clip editor + both graphs are full canvases; params live in the dock |
| **Editors are floatable** | **PASS (shipped)** | clip editor `float` button + Output pop-out (answers Open Q3) |

### C. Layout stress (resize)

- **Desktop (1600–1800px):** clean; all regions fit (Phase-1/2 shots + this session).
- **Narrow (1000×760, `05-resize-narrow-1000.png`):** **holds.** The session column keeps its fixed
  width; the visual graph takes the remainder and stays pannable (nodes near the right edge clip but
  are reachable by panning — non-destructive); the floating Output preview overlaps the graph (by
  design, ADR-0014). No overlap of controls, no lost labels.
- **Minimum (480×360, `06-resize-minimum.png`):** **BREAKS.** The **top transport bar overlaps** —
  the right-anchored perf readout ("88 fps · 11.4") collides with the BPM/transport cluster; the
  **session grid clips** to just the track-header row (scenes/clips/mixer pushed off-screen); the
  Output preview is jammed mostly off-screen. The window accepted 480×360 because **no minimum size
  is enforced** (→ F1). The threshold where the top bar first overlaps is between 1000 and 480px.

### D. State distinctness

Selection, focus, and status states are visually distinct and consistent with the palette: a
**blue** 1px border marks the selected track header / node; a **gold** border marks the active/queued
clip (transport playing → intro scene active); the bottom **detail region** shows the current focus
(audio graph vs clip editor); diagnostics uses **green/gold/red** for ok/warn/error. No ambiguous or
hidden-only selection state was observed on the release-visible surfaces.

### E. Findings

#### F1 (P3): No minimum window size — the app can be resized into a broken layout

- Surface: window creation (`app/src/main.cpp` — no `glfwSetWindowSizeLimits`).
- Impact: dragging the window small enough (below ~800px wide) overlaps the top transport bar (perf
  readout over the BPM/transport cluster) and clips the session grid to its header row; at 480×360
  most of the UI is inaccessible. Recoverable by resizing larger, but a user resizing on a small
  laptop passes through — or lands in — a broken layout with no floor. Answers this phase's Open
  Question on the minimum supported size (currently: none).
- Evidence: `evidence/phase-03/06-resize-minimum.png` (480×360, overlap + clipping) vs
  `05-resize-narrow-1000.png` (1000px, clean); `grep glfwSetWindowSizeLimits app/src` → no match.
- Smallest acceptable fix: call `glfwSetWindowSizeLimits` with a sensible floor (e.g. ~960×640, below
  which the top bar first overlaps) so the window can't be sized into the broken zone.
  Owner/status: Unassigned | P3.

#### F2 (P3): Neutral-gray popups (shader library, add-node chooser) carry no domain identity

- Surface: `ui/shader_library_view`, `ui/chooser` vs the `Domain`/`domain_color` system in
  `ui/ui_style.h`.
- Impact: ui-principles makes domain colour the zone's *identity*. The mapping overview is teal and
  diagnostics is severity-coloured, but the shader library — a **visual** content browser — and the
  add-node chooser render neutral gray. The chooser compounds this: `Tab` opens the **audio** *or*
  **visual** operator list depending on hidden focus (evidence 04 opened the audio list), with no
  domain cue on the popup, so which world you're adding to isn't self-evident (touches the failure
  mode "a view changes meaning based only on hidden selection state").
- Evidence: `03-shader-library.png` (gray, visual content), `04-add-node-chooser.png` (gray, audio
  ops from a visual-graph `Tab`).
- Smallest acceptable fix: give the shader library a visual (cyan) edge-accent, and give the chooser
  the edge-accent of the domain it is adding into. Owner/status: Unassigned | P3.

#### F3 (P3): Shader-library description column truncates mid-word without an ellipsis

- Surface: `ui/shader_library_view` — the SUMMARY/ERROR column.
- Impact: descriptions cut mid-word ("Box blur of the input texture; radius", "Blend two inputs (A
  base, B over): nor") with no ellipsis, so it reads as clipped rather than intentionally shortened —
  a small content-forward wart on an otherwise clean browser.
- Evidence: `03-shader-library.png`.
- Smallest acceptable fix: clip with a trailing "…", or widen/wrap the column. Owner/status:
  Unassigned | P3.

## Open Questions

*(answered)*

- **What is the minimum supported app window size for the first release?** Currently **none is
  enforced** (F1), which is itself the finding. Recommend defining and enforcing a floor around
  **960×640** — the top transport bar overlaps below roughly 800px, so a ~960px floor keeps a margin.
- **Which diagnostics are user-facing vs development-only?** The diagnostics panel (`h`) and the log
  view (`j`) are **user-facing** and appropriate: severity + GPU + graph/op counts + the P2-03
  "Output feeding" signal, and a leveled, colour-coded log — no developer-only internals leak into
  them. (The separate Gemini "Eval" menu surface is a Phase-1 F5 item, not part of these panels.)
- **Are floatable editors part of the release bar or a deferred promise?** **Shipped** for the
  release bar: the clip editor exposes a `float` button and the Output preview pops out to its own OS
  window (ADR-0014 / ui-principles §6). The float mechanism is real, not deferred.

## Follow-Up Plans

- **F1** — add `glfwSetWindowSizeLimits` (a one-line, self-contained fix); good candidate to batch
  with other small polish.
- **F2/F3** — shader-library + chooser domain-accent and the description-truncation ellipsis, both in
  `ui/` (self-contained, no behavior change).
- **Cross-refs:** this phase discharges the Phase-2 "every primary command has a discoverable **UI**
  path" hand-off in part — the overlays (mappings/diagnostics/log/shader-library/chooser) are all
  keybind-reachable, but discoverability of those keybinds (no in-app shortcut list; the ☰ button is
  a browser toggle, not a menu) is a **Phase-4** input/discoverability concern (ties to Phase-1 F3
  onboarding). The shared-geometry PASS here rests on the code audit's Phase-3 §D result.
