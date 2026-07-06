# ADR-0007: Node Graph Is a Contextual Deep View

Status: accepted

Date: 2026-06-17

## Context

Vivid Classic was graph-first: one project-wide node graph spanning audio, GPU, and control
operators was the primary surface. Vivid 4 makes Session View primary and demotes the graph to
"the deep implementation view" (ADR-0002), but it never said *where* that graph lives —
whether there is still one giant project-wide canvas, only scoped fragments, or both. The
question surfaced concretely: the prototype only exposed a node graph scoped to a *visual*
layer, with nothing for audio and no whole-project view.

## Decision

The node graph is **a depth, not a place** — "show me the operators behind this."

- It is reached **contextually** as the deep view of a selected session object: a visual Stage
  layer opens its operator network; an audio/instrument track opens its audio chain
  (instrument → effects → mixer); a project-local node opens its source code.
- **Audio and visual are symmetric**: the Session-grid track descent and the Stage layer
  descent are the same Network → Code mechanism over different scopes.
- A **whole-project graph** exists as an optional escape hatch (the classic-style all-in-one
  view across audio + GPU + control), for full inspection when wanted.
- The graph is **never the primary surface**. Scoped/contextual is the default path; the
  whole-project view is opt-in.

## Alternatives Considered

- **Scoped-only, no whole-project view.** Purest anti-overwhelm stance, but drops the
  occasionally-valuable ability to see the entire system at once.
- **Whole-project graph as the deep view (classic-style), scoping is just focus/filter.**
  Closest to classic, but reintroduces the giant-canvas overwhelm the reboot is escaping as
  the *only* way in.

## Consequences

- Session-first is preserved; the graph is entered by drilling from a session object, not by
  starting at a canvas.
- Audio and visual authoring stay symmetric — one descent mechanism, two domains.
- The whole-project view is a deliberate, low-frequency affordance, not the home screen.
- Project text (ADR-0006) remains the source of truth for graph structure at every scope; the
  scoped and whole-project views are both projections of the same text.
