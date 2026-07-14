# ADR-0014: The Visual Graph Is Home; The Output Is a Floatable Surface

Status: accepted

Date: 2026-07-12

Amends: [ADR-0013](ADR-0013-focus-first-strict-zone-ui.md) (focus-first, strict-zone UI) — points
3 and 5, and its UI-2 phase.

Decided: in the **visual** domain the **node graph is the persistent zone** and the **rendered
output is a floatable surface** — an in-graph preview panel that can pop out to its own OS window —
whose configuration (resolution, aspect, fit, preview/pop-out toggles) lives as **params on the
Output node**. ADR-0013's other decisions (strict domain zones, explicit focus, editors ≠
inspectors, floatability) stand unchanged.

## Context

ADR-0013 set the visual domain up as *"the visual output is the always-on canvas; the visual graph
is its depth, opened on demand."* Implemented (UI-1), that made the right column open as an OUTPUT
preview with the graph hidden behind a toggle (`Window::show_graph = false`).

Using it revealed the model is backwards **for authoring**:

- **Visual authoring *is* graph work.** Every meaningful edit — add an op, rewire, retarget a
  param, drill into an editor — happens on the graph. Putting it one toggle away taxes the primary
  activity to privilege a surface you mostly *watch*. The audio domain's home (the session grid) is
  where you author; the visual domain's home was where you spectate.
- **The output is the artifact, not the workspace.** It's the thing being made. Like a DAW's
  master meter or a video editor's program monitor, it wants to be *visible and repositionable* —
  and, for performance, on another display entirely — not to own the authoring rectangle.
- **The output had no identity.** With no resolution or aspect of its own (a fixed 720×300 render
  target, blitted stretched), "the output" was whatever rectangle the panel happened to be. Ops
  compensated with hard-coded `/1.7778` corrections in their shaders. An output you can't *size*
  isn't an always-on canvas; it's a viewport.

vivid-classic resolved all three the same way, and it is the mature precedent: the graph is the
canvas, and `video_out` — a node — carries `fit_mode`, `launch` (open the output on another
monitor) and `display_target`.

## Decision

1. **The visuals graph is the persistent visual zone.** It occupies the visual column outright — no
   enclosing "SIGNAL · VISUALS" panel, no reveal toggle. Opening the app shows the graph.

2. **The rendered output is a floating surface, not a zone.** It renders into a floating preview
   panel over the graph (movable, resizable, closable), and can pop out to its own OS window for a
   second display. Docked-preview and popped-out are the same output.

3. **The Output node owns the output's identity.** Resolution, aspect ratio, fit (Fit/Fill/Stretch),
   whether the preview is shown, and whether the pop-out window is launched (and on which display)
   are **params on the Output node** — the same mechanism as any other operator param. They are
   therefore inspectable, mappable, persisted, and MCP-addressable for free, with no bespoke
   endpoints. Selecting the Output node *is* the output settings UI.

4. **The render target has a true aspect.** `VisualGraph`'s render targets are allocated at the
   Output node's resolution and letterboxed (per `fit`) into whatever surface presents them.
   Operators derive aspect from their real render-target dimensions; no shader hard-codes a display
   aspect.

5. **ADR-0013 otherwise stands.** Strict domain zones (audio ≠ visual rectangles), the explicit
   `FocusContext` detail region, editors ≠ inspectors, and float-out all continue. This ADR changes
   *which* visual surface is persistent, not the principles governing the rest of the shell.

## Consequences

- **Positive:** the primary visual activity is on screen at launch; the graph gets real canvas room;
  the output becomes a first-class, configurable object (a real resolution you can name) instead of
  an implicit viewport; second-monitor performance output falls out of the same node; the four
  `/1.7778` shader hacks disappear along with the distortion that motivated them.
- **Cost / churn:** the visual column's layout, the frame loop's render target rect, and the
  visuals input dispatch all change; `show_graph` and the SIGNAL/OUTPUT panel geometry are removed.
  The always-on-output guarantee of ADR-0013 is relaxed — the preview can be closed. That is
  deliberate: an artist who wants the output always visible pops it out to its own display.
- **Asymmetry, on purpose:** the audio domain keeps *session-is-home, graph-is-depth* (ADR-0007);
  the visual domain gets *graph-is-home*. This is "parity, not symmetry" (PRD) — each domain gets
  the interface model its authoring loop deserves. The domains remain strictly zoned, so the
  asymmetry never shows up as ambiguity about which world you're in.

## Alternatives considered

- **Keep ADR-0013 as written** (output always-on, graph behind a toggle). Rejected by use: it taxes
  every visual edit to privilege a passive surface.
- **Split the column: output on top, graph below** (today's `show_graph = true` state). Rejected —
  it's the crowding ADR-0013 was written to end, and it starves the graph of the space it needs.
- **Output settings in a preferences dialog or the transport bar.** Rejected: the output is a node
  in the graph; its settings belong on it (classic's `video_out` precedent), which also gets us
  persistence, mapping, and MCP for free.
