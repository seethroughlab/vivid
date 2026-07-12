# Vivid-4 UI Principles

The rules that keep the interface legible as it grows. They exist because the app grew
organically (P9→AG) into a crowded, domain-confused UI that had drifted from its own docs —
most visibly, the node graph became a permanent *place* and the bottom dock became one
overloaded region that implicitly swapped between audio and visual controls. These principles,
ratified in **[ADR-0013](../decisions/ADR-0013-focus-first-strict-zone-ui.md)** (which also
defines the target information architecture), pull the interface back to the product's intent.

New UI must satisfy these. When a change can't, that's a signal to revisit the principle
explicitly (an ADR), not to quietly violate it.

## Reaffirmed (from the PRD + earlier ADRs)

- **Two surfaces, one transport.** Audio and visuals each get the interface model their domain
  deserves; they share one musical transport. *(PRD)*
- **Parity, not symmetry.** Both domains are equally expressive and inspectable, but need not
  share an interface model. *(PRD)*
- **See every step.** Every meaningful layer is inspectable; favor explainable state over hidden
  magic. *(PRD)*
- **Content-forward, no decorative chrome.** The content and layout communicate; no ornamentation.
  *(PRD, and vivid-classic's "content is the star".)*
- **Experimentation-first.** Cheap to audition, inspect, keep, branch, compare. *(PRD)*
- **The bridge is a first-class, visible object.** Audio↔visual mappings are an editable product
  object, not a hidden implementation detail. *(ADR-0009)*
- **Text is the source of truth; the GUI is a view.** The UI never becomes a second store.
  *(ADR-0006)*
- **Discipline through constraint.** All UI uses `Renderer2D` + the `ui_style` palette; geometry
  lives once in `ui/layout.h`, shared by draw and hit-test. *(app/src/ui/CLAUDE.md)*

## Sharpened by ADR-0013

1. **In the audio domain, session is home; graphs and rich editors are deep views you drill into.**
   A track's audio graph is *"a depth, not a place"* — reached contextually from a selected object,
   never a permanent pane competing for space. *(Recommits
   [ADR-0002](../decisions/ADR-0002-session-view-first.md) +
   [ADR-0007](../decisions/ADR-0007-node-graph-contextual-deep-view.md).)* The **visual** domain
   inverts this — see principle 5 — because visual authoring happens *on* the graph.

2. **One focused editor at a time (progressive disclosure).** An explicit *focus context* — what
   the user chose to open — drives a single detail region. The UI never multiplexes a region
   implicitly by "whatever was last selected." Selection (what's highlighted) and focus (what the
   detail region shows) are distinct concepts.

3. **Strict domain zones.** Audio and visual controls never share a rectangle. Every region has a
   domain identity, made obvious by position and the domain color system (audio **amber** ·
   visual **cyan** · bridge **teal** · shared **gray** — `Domain` / `domain_color` in
   `ui_style.h`). You always know which world you're in by where you're looking.

4. **Editors ≠ inspectors.** A *rich editor* is a per-object canvas (a clip's piano-roll, a
   track's audio graph, a drum grid, an envelope) that gets its own space and can float out. An
   *inspector* is a lightweight, host-composed strip of the current selection's params, rendered
   from operator metadata via a compound-widget registry (ADSR / XY-pad / color / LFO). Don't cram
   a rich editor into inspector param-rows, and don't make inspectors bespoke per operator.

5. **In the visual domain, the graph is home and the output is a floatable surface.** Visual
   authoring *is* graph work, so the visuals graph is the persistent visual zone. The rendered
   output — the artifact — is a floating preview over it that can pop out to its own OS window
   (second display, live performance). Its identity (resolution, aspect, fit, preview/pop-out) lives
   as **params on the Output node**, not in app chrome — so it is inspectable, persisted, and
   MCP-addressable like any operator param. *(Amended by
   [ADR-0014](../decisions/ADR-0014-visual-graph-is-home.md), which replaces ADR-0013's
   "output is the always-on canvas, graph is its depth". Audio keeps session-is-home,
   graph-is-depth — parity, not symmetry.)*

6. **Editors are floatable.** A deep view can pop out to its own OS window (second monitor, live
   performance), reusing the App/Window seam. Docked and floating are the same editor.

## Visual language (the vivid-classic "serious instrument" look)

The interface should read like a professional tool (Ableton / TouchDesigner), not a website. The
visual vocabulary lives entirely in `ui_style.h` (tokens + shared helpers built on `Renderer2D`);
keep new UI on those so the restyle stays coherent.

- **Hard 90° angles.** `radius = radius_lg = 0`. No rounded cards or pills. Corners are crisp.
- **Flat panels framed by 1px rules, not nested filled cards.** A surface is a flat fill +
  `draw_rect_outline` (1px `border`) + edge accent bar. Structure comes from **1px separator rules
  and negative space**, not boxes-inside-boxes. Keep container nesting shallow (≤2 layers).
- **Dark steel palette.** Near-black cool surfaces (`bg`/`region`/`card`/`recess`), a single steel
  `border`. Flat fills — no gradients; the only shadow is `draw_shadow` (a lo-fi 2-layer offset)
  behind popups/menus.
- **Domain color = identity; blue = selection.** The domain accents (audio **amber** · visual
  **cyan** · bridge **teal**) are the strict-zone identity, shown as a thin **edge accent bar**.
  Selection/focus is the classic blue `sel` (`#5A8CD9`) as a 1px border or 2px edge — never a
  separate rounded ring. `gold` is reserved for queued/warn state.
- **Framed, highlighted boxes.** Interactive items are flat boxes with a hard 1px frame; the frame
  goes blue when selected. Highlight through the frame + fill, not padding or rounding.

## How to apply

- Adding a new view? It is either a **persistent zone** (rare — needs a domain and a permanent
  place, e.g. the session or the output), a **deep view** hosted in the detail region (most
  editors), or an **inspector widget** (params). Pick one; don't invent a new always-visible pane.
- Never route two domains through one region by selection state. If you need audio *and* visual
  visible together, they are two zones, not one multiplexed rectangle.
- A rich per-operator UI belongs in an operator **editor** (its own canvas), not in the inspector.
- Prove it local first (vivid-classic's guardrail): a new UI idea lives inside one operator/editor
  before it becomes product-wide architecture.
