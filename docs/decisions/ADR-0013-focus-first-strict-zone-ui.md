# ADR-0013: Focus-First, Strict-Zone UI — Session Home, Deep-View Editors, Explicit Focus

Status: proposed

Date: 2026-07-06

Follows: [ADR-0002](ADR-0002-session-view-first.md) (session-view-first),
[ADR-0007](ADR-0007-node-graph-contextual-deep-view.md) (node graph as a contextual deep view),
[ADR-0009](ADR-0009-two-surface-bridge-and-cpp-poc.md) (two surfaces + bridge)

Decided: the interface is reorganized around **strict domain zones**, an **always-on visual
output**, and **one focused deep-view editor at a time** driven by an **explicit focus context**
— replacing the overloaded bottom dock that implicitly multiplexed audio, visual, and clip
editing by last-selection. Principles are recorded in
[`../product/ui-principles.md`](../product/ui-principles.md).

## Context

Vivid-4's UI grew organically (P9→P28, MT, M0–M6, AO-0..4, AG-0) and **drifted from its own
documented principles**. ADR-0002/0007 say the node graph is *"a depth, not a place"* — a
contextual deep view drilled into from a selected object. But the implementation made the visuals
graph a **permanent right-hand pane**, and the bottom device dock a single **multiplex valve**
that swaps between three unrelated things — the selected visual node's params, the selected audio
track's device chain, and a docked clip editor — based on whichever was selected last
(`session_view.cpp:draw_device_dock` branching on `graph->selected_op()` vs `sel_track` vs
`clip_editor.is_docked()`; the long serial dispatch in `input.cpp`). The consequences:

- **Audio and visual controls share one rectangle**, switching implicitly. You cannot tell which
  domain you're editing from where you're looking.
- **There is no explicit "what am I editing" state** — focus is inferred from the last click, so
  you can't pin a view, and selecting a visual node silently hijacks the dock from a track.
- **New deep views have nowhere to go.** The per-track audio graph (AG) was about to be crammed
  into the same dock, compounding the overload; the clip editor already *occludes* the dock.

A holistic review (three read-only probes: existing vivid-4 principles, vivid-classic's UI
paradigm, and a current-state crowding inventory) plus a decision session with the product owner
produced the direction below.

## Decision

1. **Session is home; graphs and rich editors are deep views.** No permanent graph pane. The
   session (audio) and the live output (visual) are the persistent zones; every graph/editor is
   opened contextually. (Recommits ADR-0002/0007.)

2. **Strict domain zones.** Audio and visual controls never share a rectangle. Every region has a
   domain identity via position + the color system (audio amber · visual cyan · bridge teal ·
   shared gray — `Domain`/`domain_color`, `ui_style.h`). The bridge is the explicit seam.

3. **The visual output is always on.** The rendered output is a persistent zone; the visual graph
   is its depth.

4. **One focused editor at a time, driven by an explicit focus context.** A single
   `FocusContext { domain, kind, object_id }` (on `Window`) replaces the implicit selection race.
   The **detail region** renders exactly one deep view from it (clip editor, a track's audio
   graph, the visual graph, a rich operator editor), with a domain tint + breadcrumb + close/float.
   Drilling in sets the focus; Esc/breadcrumb clears it. **Selection ≠ focus.**

5. **Editors ≠ inspectors** (both re-adopted from vivid-classic). Rich per-object editors get their
   own (floatable) space; a lightweight host-composed **compound-widget inspector**
   (ADSR/XY-pad/color/LFO, declared by operator metadata) shows the selection's params.

6. **Editors are floatable** to their own OS windows via the App/Window seam (the pop-out visuals
   window is the precedent).

## Target information architecture

```
┌─ Transport (shared) ──────────────────────────────────────────────────────┐
├─ SESSION (audio zone, home)               │ OUTPUT (visual zone, always-on) │
│  tracks × scenes + mixer [+ sidebar]      │ live rendered canvas            │
├───────────────────────────────────────────┴─────────────────────────────────┤
│ DETAIL REGION — one focused deep view (domain-tinted + breadcrumb + float):  │
│   clip editor · track audio graph · visual graph · rich operator editor      │
│ + a thin INSPECTOR strip: the selection's compound-widget params             │
└──────────────────────────────────────────────────────────────────────────────┘
```

## Consequences

- **Positive:** the audio/visual boundary becomes structural and always legible; the overloaded
  dock is dissolved; new deep views (the audio graph, operator editors) have a real home;
  focus is explicit, so views can be pinned and floated; the app matches its own docs again.
- **Cost / churn:** the focus-model refactor touches the input dispatch chain + the dock (the
  highest-churn change) — mitigated by migrating one deep view (the clip editor) behind the
  existing behavior before removing the multiplex, keeping the app usable each phase.
- **Workflow change:** the always-visible visuals graph is retired in favor of drill-in + the
  always-on output + optional float-out — a deliberate trade to end the crowding.

## Alternatives considered

- **Keep graphs as permanent panes; solve crowding with tabs/zones.** Rejected: doubles down on
  the drift from ADR-0007 and keeps two large panes competing for space.
- **Dense, everything-visible layout** (modular-desk style). Rejected in favor of focus-first
  progressive disclosure — the crowding is precisely too-much-at-once.
- **Unified inspector that shows any domain, color-coded only.** Rejected as the primary model
  (it's the current sin); strict spatial zones are the decision. A single domain-tinted detail
  region showing one deep view at a time is the bounded exception, made unambiguous by color +
  breadcrumb.

## Phasing

UI-0 principles + this ADR + the domain color helper; UI-1 explicit focus model + detail-region
host (migrate the clip editor first); UI-2 visuals graph → deep view + promote the output to a
persistent zone; UI-3 device chain → inspector + the audio graph as a deep view (this is where the
AG audio-graph UI lands, superseding the "graph in the dock" plan); UI-4 the editors + inspectors
ABI; UI-5 float-out windows; UI-6 verify + prune. The audio-graph **engine** (AG-0/AG-1) is
unaffected; only the AG *UI* placement changes.
