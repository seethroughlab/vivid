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

1. **Session is home; graphs and rich editors are deep views you drill into.** The node graph is
   *"a depth, not a place"* — reached contextually from a selected object, never a permanent pane
   competing for space. *(Recommits [ADR-0002](../decisions/ADR-0002-session-view-first.md) +
   [ADR-0007](../decisions/ADR-0007-node-graph-contextual-deep-view.md).)*

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

5. **The visual output is the always-on canvas.** The rendered output — the thing you're making —
   is persistently on screen. The visual graph is its *depth*, opened on demand.

6. **Editors are floatable.** A deep view can pop out to its own OS window (second monitor, live
   performance), reusing the App/Window seam. Docked and floating are the same editor.

## How to apply

- Adding a new view? It is either a **persistent zone** (rare — needs a domain and a permanent
  place, e.g. the session or the output), a **deep view** hosted in the detail region (most
  editors), or an **inspector widget** (params). Pick one; don't invent a new always-visible pane.
- Never route two domains through one region by selection state. If you need audio *and* visual
  visible together, they are two zones, not one multiplexed rectangle.
- A rich per-operator UI belongs in an operator **editor** (its own canvas), not in the inspector.
- Prove it local first (vivid-classic's guardrail): a new UI idea lives inside one operator/editor
  before it becomes product-wide architecture.
