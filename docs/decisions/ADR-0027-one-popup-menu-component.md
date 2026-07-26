# ADR-0027: One Popup-Menu Component

Status: accepted (2026-07-25)

Date: 2026-07-25

Extends [ADR-0025](ADR-0025-cpp17-organization-and-patterns.md) (pressure-point #2: move interaction
ownership out of `Window`) and [ADR-0023](ADR-0023-shared-graph-ui-substrate.md) (shared UI substrate).

## Context

Right-click / contextual popups accreted one at a time as features landed, and there are now five
near-parallel implementations, each a bespoke `{open, x, y, …}` struct on `Window` with its own draw
function and its own click-hit handler:

- `CtxMenu menu` — the track "characteristics" meter menu (`app/src/app/window.h:24`).
- `CtxMenu map_menu` — the bridge map-source picker (same struct, reused).
- `NodeMenu node_menu` — right-click on a visuals op node.
- `AudioNodeMenu audio_node_menu` — right-click on an audio-graph node ("→ visuals").
- `ModEditor mod_editor` — the modulation-shape popover (a popup in the same family).

The costs are concrete, not cosmetic:

- **An overloaded field.** `CtxMenu.src` means a *track index* for `menu` but a *node id* for `map_menu`
  — the same type used two incompatible ways, disambiguated only by which variable holds it.
- **Draw ⇄ hit-test duplication.** Each menu has a hand-rolled `draw_*` in `session_view.cpp` and a matching
  `*_menu_click` / `*_rclick` in `input_dock.cpp` / `input_graph.cpp` whose row geometry must stay
  byte-aligned with the draw code by inspection. The item catalogs live in yet another place
  (`kChars` / `kAudioNodeChars` / `kModNodeChars` in `layout.h`).
- **Five places to change one behavior.** Keyboard dismissal, click-outside-to-close, hover highlight,
  and off-screen clamping are re-implemented (or silently missing) per menu.

This is exactly ADR-0025's pressure-point #2 — interaction state that should live in a small persistent
owner is instead spread across the `Window` state bag and three input/draw files.

## Decision

Introduce **one `PopupMenu` component** that owns a popup's open/position state, its item list, its draw,
and its hit-testing, and migrate the five existing menus onto it.

1. **A single value type.** `PopupMenu` holds `{open, x, y, items, hovered}` and a small typed **payload**
   (e.g. a `std::variant` or a tagged `{PopupKind kind; int a; int b;}`) that replaces the overloaded
   `CtxMenu.src`. The kind, not the variable name, says whether the ints are a track index, a node id, or a
   `(track, node)` pair.

2. **Items are data.** An item is `{label, enabled, action}` (or a stable id the caller dispatches on). The
   `kChars` / `kAudioNodeChars` / `kModNodeChars` catalogs become item-list builders that return
   `std::vector<PopupItem>`, so the source-of-truth for "what's on this menu" is one list, not a table plus
   parallel draw/hit code.

3. **Draw and hit-test share geometry.** One `draw(ui, menu)` and one `hit(menu, mx, my) -> item*` compute
   row rects from the same layout function, so they cannot drift. Dismissal, hover, and off-screen clamp
   live here once.

4. **Migrate incrementally, behavior-preserving.** Convert one menu per PR (start with the two `CtxMenu`
   uses, since they share a struct and best demonstrate the payload replacing `.src`), each landing green
   with no user-visible change. `ModEditor` is a richer popover (it hosts a curve editor, not just rows); it
   adopts the shared open/position/dismiss/clamp scaffolding even if it keeps a custom body.

This follows ADR-0023's substrate pattern (shared `NodeView`/`CardPorts`/`GraphCanvas`) applied to menus,
and ADR-0025's "reduce large state hubs incrementally" — `Window` sheds five ad-hoc structs for one owned
component.

## Alternatives Considered

- **Leave the five menus as-is.** Rejected. They already caused one real class of bug (overloaded `.src`)
  and every new context menu pays the full draw+hit+catalog tax. The count is only going up.
- **Adopt an immediate-mode GUI library (Dear ImGui, etc.) for menus.** Rejected. Vivid draws through its
  own `renderer_2d` with a deliberate visual identity; pulling in a second UI toolkit for popups alone
  would fracture the look and the input model for a small surface.
- **One giant menu enum with a switch per call site.** Rejected — that keeps the duplication and just moves
  the overloaded-field problem into a bigger switch. The win is a *component* that owns draw+hit together,
  not a shared enum.

## Consequences

- **Positive:** One place to fix dismissal, hover, clamping, and keyboard handling; adding a context menu
  becomes "build an item list," not "write a struct + draw + hit-test + catalog."
- **Positive:** The overloaded `CtxMenu.src` disappears — payload kind is explicit and type-checked.
- **Positive:** `Window` loses five interaction structs, advancing ADR-0025 pressure-point #2.
- **Tradeoff:** A migration with five call sites; each conversion must be verified pixel-for-behavior against
  the current menu (headless where possible — row-geometry hit math is pure and testable — plus a live
  right-click spot check).
- **Tradeoff:** `ModEditor`'s custom body means the abstraction must not assume "menus are only rows"; the
  shared part is the popup *frame* (state + placement + dismiss), not necessarily the contents.
- **Follow-up:** Once migrated, a headless test over the shared hit-test (rows in → item out, with
  off-screen clamp) locks the geometry so future edits can't silently misalign draw and hit.
