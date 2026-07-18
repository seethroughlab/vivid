# Graph UI Abstraction Plan

Status: draft — Phase 1 scope
Date: 2026-07-18
Implements [ADR-0023](../decisions/ADR-0023-shared-graph-ui-substrate.md) (shared graph UI
substrate). This is the document ADR-0023 migration step 1 asks for: it defines the
adapter / canvas / controller contracts and stages the migration so each phase is
behavior-preserving and independently verifiable.

## Why this document exists

ADR-0023 says the two graph editors "share a look and some widgets, but they do not share a
graph-editor contract." Before extracting anything, we need an honest map of what is *already*
shared, what genuinely diverges, and what the target contracts are — otherwise the extraction
risks either re-doing work already done in `node_canvas.h` or force-unifying two views whose
divergence is real.

## Current state (honest map)

### What `ui/node_canvas.h` already owns (the existing substrate, ~123 lines)

The shared *drawing vocabulary and view math* is already extracted and used by both editors:

- `NodeView` — the pan/zoom world↔screen transform (`to_world`, `zoom_at`, `pan`), with the
  0.35–3.0 zoom clamp.
- `node_wire` — the bezier wire (also the ghost-wire shape).
- `node_port` — the square port nub.
- `node_grid` — the world-space background grid.
- `node_card` — the steel card + header strip, drawn through `item_box` (so selection is the
  global blue `ItemBox` frame).
- Broken-node vocabulary (ADR-0019): `node_error_border`, `node_error_badge[_rect]`,
  `node_error_label_shift`, `node_error_note`.
- `node_preview_panel` (a `recess` well) + `node_waveform`.

**Implication:** the "shared canvas" (ADR-0023 layer 2) already exists in embryo. Phase 1 is
mostly about *adopting* it fully and defining the two layers that do not yet exist (the model
adapter and the interaction controller), not about building the canvas from scratch.

### The two editors have different architectures

| | `NodeGraph` (visual) | `AudioNodeGraph` (audio) |
|---|---|---|
| File size | `node_graph.h` 196 / `.cpp` 791 | `audio_node_graph.h` 137 / `.cpp` 606 |
| Interaction state | **Owns a drag state machine** (`drag_mode_` 0–5, `drag_idx_`, `wire_from_`, `on_down/on_move/on_up`) | **Stateless view** — holds no per-frame interaction state; `layout()` is a pure function of (session, track, bounds). Interaction lives in `app/input_graph.cpp` (~653 lines), which rebuilds a throwaway `AudioNodeGraph` per event and stores drag state in `Window.ag_*` fields |
| View transform | private `view_ox_/view_oy_/view_scale_` **+ its own `to_world`** (a duplicate of `NodeView`) | private `zoom_ / pan_x_ / pan_y_`, applied around the region origin inside `layout()` |
| Model | owns `VisualGraph*` + the `MappingRegistry` (the bridge) + persistence helpers | reads `Session` introspection; borrows the visual graph's `MappingRegistry` via `map_` for the mapped-state dot |
| Hit-test | point-out helpers (`op_in_port`, `op_out_port`, `param_port`, `nearest_*`) | `Rect`-returning helpers (`out_port_rect`, `in_port_rect`, `param_port_rect`, `param_port_hit`) |
| Card port layout | inputs stacked left, output right, param ports per row | signal-in top-left, param ports down the left edge, output right, plugin "+" row |

### Three real divergences Phase 1 must name (not yet resolve)

1. **Two view-transform conventions.** `NodeGraph` is already `NodeView`-shaped (offset + scale)
   and literally re-implements `to_world`; adopting `NodeView` there is mechanical.
   `AudioNodeGraph` uses zoom-around-region-origin + pan, folded into `layout()` — a different
   convention that needs a reconciling wrapper, not a drop-in swap.
2. **Two interaction-ownership models.** Visual owns its drag machine; audio delegates to
   `app/input_graph.cpp` + `Window.ag_*`. The `GraphInteractionController` (layer 3) must be
   able to host *both* the owned-state and the stateless-view style, or the migration will stall
   on the audio side.
3. **Two card port layouts, and a duplicated layout algorithm.** The port geometry differs by
   design (a plugin "+" row, key-range handles, signal-vs-param ports) and even the row pitch
   differs (18px visual / 15px audio). Worse, the layered left→right *rank* layout is
   independently reimplemented in each editor (`node_graph.cpp` `layout_nodes` vs
   `audio_node_graph.cpp` `layout`). A single shared port-layout primitive and a shared
   layout pass are Phase 2/3 goals, **not** behavior-preserving Phase 1 changes.

### What is already shared and must NOT be re-extracted

- The view math (`NodeView`) — exists; just not yet adopted by the visual editor.
- Ghost-wire (`node_wire`), selection ring (`node_card`→`item_box`), error vocabulary, preview
  well (`recess`), waveform.
- The Tab palette: `Chooser` + `ChooserEntry` is already generic over both graphs, and
  `Chooser::show(cursor, bounds)` already is the chooser-anchoring primitive. `ChooserEntry`
  (`label/id/summary/badge/hay/tag/enabled/accent`) is already the "one addable-node contract"
  ADR-0023 layer 5 asks for — the audio side builds it from `audio_catalog.h`, the visual side
  from the operator registry.

## Target contracts (ADR-0023 layers 1–3)

These are interface sketches, refined as code is extracted. They are the point of this document.

### Layer 1 — `GraphModelAdapter` (the UI-facing view of a domain model)

A read-mostly view interface the shared canvas/controller draw and hit-test against. Both editors
already answer every one of these questions internally; the adapter is the common shape:

- Nodes: count, stable id per node, world rect (position + size), title/kind label.
- Badges/health: error state + first-line message, quarantine, active-output ring.
- Ports: per node, the input ports, the output port, the param ports (with mapped/wired flag).
- Params: per node, count, label, type/hint/min/max/choices, base value, resolved value.
- Selection: the selected node id.
- Preview: optional per-node preview payload (GPU texture handle for visual, waveform samples
  for audio) — drawn by a domain hook inside the shared preview slot.

Command callbacks (writes the controller invokes; each domain supplies its own):
connect edge, disconnect, set param base, add node, remove node, expose plugin param,
set node position.

### Layer 2 — `GraphCanvas` (reusable draw + geometry, built on `node_canvas.h`)

Owns graph-region geometry, the `NodeView` transform, grid/card/wire/port/selection/ghost-wire
drawing, the preview slot, and hit-test primitives (node-at-point, port-at-point). It calls the
adapter for *what* to draw and where; domain overlays (thumbnails, waveforms, key ranges, plugin
pin rows, compound widgets) render into canvas-provided slots.

### Layer 3 — `GraphInteractionController` (the drag/select/pan/rewire state machine)

Hosts the interaction state currently living in `NodeGraph` (the `drag_mode_` machine) and in
`input.cpp` (for audio). Translates gestures into adapter command callbacks. Must support both
the owned-state editor and the stateless view without forcing either to invert first.

## Phased migration (maps ADR-0023 migration steps → phases)

| Phase | ADR-0023 steps | Behavior change? | Gist |
|---|---|---|---|
| **1 (this scope)** | 1–2 | **No** | This doc + adopt the existing substrate fully; extract only provably-identical primitives |
| 2 | 3 | No (view move) | Move `AudioNodeGraph` draw + hit-test onto `GraphCanvas` (it is already a pure view — the safest first migration) |
| 3 | 4 | No (view move) | Move `NodeGraph` draw + hit-test onto `GraphCanvas` in small pieces; keep mapping/shader/persistence ownership put |
| 4 | 5 | No | Normalize graph catalogs + `ChooserEntry` construction (domain/kind/spawn) after both consume the canvas |
| 5 | 6 | No | Split `input.cpp`: GLFW install + modal priority stays; graph gestures move behind the controllers |
| 6 | 7 | Additive | Add `list_operator_catalog(domain?)`; keep `list_operators` + audio discovery as compat wrappers; migrate MCP parity tests last |

## Phase 1 — concrete scope

Goal: land the contracts (this doc) and pull both editors fully onto the *existing* substrate,
with **zero user-visible behavior change** and no control-API change. This de-risks Phases 2–3
by proving the direction on the smallest, safest surface.

### 1.0 — Contracts document *(this file)*
Define the adapter/canvas/controller contracts and the three divergences above. Done when
reviewed.

### 1.1 — Adopt `NodeView` in the visual editor
Replace `NodeGraph`'s `view_ox_/view_oy_/view_scale_` and its private `to_world` with a
`NodeView view_;`. Forward `get_view/set_view/zoom_at` to it. Mechanical — the shapes match.
Watch: view persistence (`get_view/set_view` round-trip) must be byte-identical.

### 1.2 — Reconcile the audio editor's view transform *(decided: defer to Phase 2)*
The `.cpp` math confirms the audio transform *is* already `NodeView`-equivalent: `layout()` bakes
`screen = graph_region_origin + world*zoom_ + pan` into each box rect
(`audio_node_graph.cpp:329`), i.e. `NodeView{ ox: g.x+pan_x_, oy: g.y+pan_y_, scale: zoom_ }`.
But its stored form (`zoom_/pan_x_/pan_y_`) lives in `Window.ag_*` and is driven by
`input_graph.cpp`, and it is rebuilt per-frame relative to `graph_region()`. Converting the stored
representation touches input routing + `Window` + persistence — not a "no behavior change" edit.
**Decision:** leave the audio stored transform as-is in Phase 1; when `AudioNodeGraph` moves onto
`GraphCanvas` in Phase 2, the canvas derives its `NodeView` from `(graph_region, zoom, pan)` at
draw time. Phase 1 touches only the visual editor.

### 1.3 — Confirm the already-shared primitives are the *only* path *(done)*
Audited both editors. **Finding:** ghost wire, selection ring, preview well, error vocabulary,
and ports already route *exclusively* through the shared primitives in both — `node_wire`
(visual `node_graph.cpp` drag-preview + chain wires; audio via the `wire()` forwarder),
`node_card`→`item_box` (selection), `node_preview_panel` (+ `node_waveform` on the audio side),
`node_error_border/badge/note`, and `node_port`. There is **no** raw `draw_polyline` wire-drawing
or bespoke selection/preview painting left in either editor. Nothing to re-route.

**One provably-identical duplicate found and removed:** `NodeGraph::in_rect(rx,ry,rw,rh,x,y)` was
a private reimplementation of the canonical half-open point-in-rect test that already lives in
`layout.h` as `hit(const Rect&, mx, my)` — the helper the audio editor and the rest of the shell
already use. Replaced the 4 call sites with `hit({…}, wx, wy)` and deleted the method. Behavior
identical; build + 49/49 tests green.

**Deliberately left alone (Phase 2/3):** the divergent card port layouts (18px vs 15px row pitch,
signal-vs-param ports, plugin "+" row, key-range handles) and the two independently-implemented
rank-layout passes. These are *similar concepts*, not *identical code* — unifying them changes
geometry and is out of scope for a no-behavior-change phase.

### Explicitly NOT in Phase 1
- Moving either editor's draw/hit-test onto a new `GraphCanvas` class (Phases 2–3).
- A unified card port layout / shared port-layout primitive (Phases 2–3).
- Catalog/`ChooserEntry` normalization (Phase 4) and input-routing split (Phase 5).
- The `list_operator_catalog` MCP endpoint (Phase 6) — **no control-server change in Phase 1**,
  so the MCP parity guard is untouched.
- Container-vocabulary convergence (`WorkspaceCanvas`/`ZonePanel`/`ItemBox`…). That is the
  *panel-consolidation audit's* own Phase 2, a separate workstream; ADR-0023 layer 7 only asks
  the graph substrate to **align** with it, and `node_card` already draws through `item_box`.

### Phase 1 acceptance / verification
- Native build green; native tests green; graph/editor interaction tests green.
- Pan / zoom / node-drag / select / wire, in **both** graphs, behave identically to pre-change
  (manual smoke, and screenshot diff where practical).
- Session persistence round-trips unchanged (view transform included) — or, if the transform
  representation must change on disk, a persist migration + test lands with it.
- No control-server / MCP surface change → MCP parity guard unaffected.

## Phase 2 — move the audio graph's draw + hit-test onto the shared canvas

Goal (ADR migration step 3): the audio editor's **graph area** — edges, cards, ports, the preview
well, the ghost wire — draws and hit-tests through shared substrate code, not audio-private
geometry. `AudioNodeGraph` is the right first target because it is already a deterministic view
over session introspection.

### What Phase 2 does *not* touch
- **The param strip** (plugin pinned inspector, ADSR/LFO previews, key-range handles, knob grid,
  bridge map dots) is audio-specific inspector UI — it stays in the audio module as a domain hook.
- **The 3-file interaction split.** Audio interaction lives in `app/input_graph.cpp` (press) +
  `app/frame.cpp` (`update_drag_continuations`, move) + `app/input.cpp` (release/dispatch), over
  13+ `Window.ag_*` fields. Folding that back into one on_down/on_move/on_up owner is ADR migration
  **step 6 (Phase 5)** — *not* here. Phase 2 keeps the existing interaction wiring and only changes
  the geometry/draw it calls into.
- **No control-server / MCP change** → parity guard untouched.

### The two things the canvas must own that aren't shared yet
1. **The view transform.** `layout()` bakes `screen = graph_region + world*zoom + pan` inline
   (`audio_node_graph.cpp:329`); the same math is re-derived for zoom (`input_graph.cpp` scroll),
   the node-drag inverse (`frame.cpp`), and draw priming (`session_view.cpp` `set_view`) — the
   documented "MUST match the draw" hazard. The audio transform is **region-relative and not
   persisted** (visual is absolute + persisted), so the canvas must *derive* a `NodeView` from
   `(graph_region, zoom, pan)` rather than store an absolute one. (This is the deferred P1.2.)
2. **The card port-row geometry.** `port_row_cy` / `card_height` / `in_port_rect` /
   `param_port_rect` / `add_param_port_rect` and the draw-side preview-well math all encode the
   same "header → N port rows → preview well" card layout with its own metrics (22px header, 15px
   rows). Draw and hit-test independently re-derive it; a shared primitive removes the drift risk.

### Sub-steps (each build+test green, behavior-preserving, verified by rendering/driving the audio graph)

- **P2.2 — shared card port-row layout primitive** *(recommended first: most contained, on-target
  for "hit-test onto canvas", pure geometry with exact metrics).* Add a parameterized card-layout
  helper to `node_canvas.h` (given card rect + header/row heights + signal-in? + exposed-param
  count + add-row? → the signal-in rect, each param-port rect, the add-row rect, the preview-well
  rect). Route audio's port accessors + the draw-side preview math through it so they cannot drift.
- **P2.1 — view-transform consolidation.** Add `NodeView::to_screen` (world→screen) and an
  `audio_view(region, zoom, pan) → NodeView` derivation to `node_canvas.h`. Route `layout()`, the
  zoom, the node-drag inverse, and the hit-test priming through it. Kills the "MUST match" hazard;
  discharges deferred P1.2 in the region-relative way.
- **P2.3 — `GraphCanvas` / adapter — REORDERED: do Phase 3 first.** Closer reading of both draw
  loops showed they differ enough (audio draws edges card-mid→card-mid; the visual graph draws
  port→port + data→param wires; different card layouts) that a fully-generic `GraphCanvas` built
  from the audio editor alone would be shaped around audio and need rework once the visual editor
  arrives. The three genuinely-shared primitives (marks, `NodeView`, `CardPorts`) are already
  extracted. **Decision: adopt those shared primitives in the visual editor next (Phase 3), so both
  editors sit on the same base, THEN extract the common draw skeleton into `GraphCanvas` from BOTH
  — non-speculative, no rework.** The `GraphCanvas` extraction becomes a later step (P4-ish).

## Phase 3 — the visual editor adopts the shared primitives

Mirror of Phase 2, on `NodeGraph`. It already adopted `NodeView` (P1.1) and routes through the
shared marks + `hit` (P1.3), so the remaining work is the **card row geometry**.

Finding: `CardPorts` fits the visual card's *row* geometry exactly once generalized —
`sig_in` (bool) → `lead_rows` (int: audio 0–1 signal-in; visual 0–2 texture inputs), and
`prev_h` → `tail_h`/`tail_pad` (audio: preview well 30+6; visual: thumbnail 46+8, or sink 0+6).
Then `height()` and `row_cy()` reproduce `op_node_rect` height, `op_row_y`, `op_in_port`, and
`param_port` exactly. The preview/thumbnail *rect* does NOT generalize (visual thumbnail is
fixed-height at +2; audio well fills-to-bottom at +1) — it stays each editor's local geometry; only
the well *mark* (`node_preview_panel`) is shared. Hit-test *method* also stays per-editor (visual
uses distance/`hypot`, audio uses rects).

- **P3.1 — generalize `CardPorts`** (`sig_in`→`lead_rows`; add `tail_h`/`tail_pad`); update audio's
  `card_ports()` to the new fields (behavior-preserving). 
- **P3.2 — adopt `CardPorts` in `NodeGraph`**: `op_node_rect` height, `op_row_y`, `op_in_port`,
  `param_port` route through a visual `card_ports(i)`; thumbnail rect stays local.
- Verify BOTH editors render identically (audio + visual graph smokes).

## Step 6 — the audio editor becomes a stateful interaction owner (do BEFORE `GraphCanvas`)

Discovered when starting the `GraphCanvas` extraction: the shared substrate (marks · `NodeView` ·
`CardPorts`) is already factored, and a `GraphCanvas` *class* is blocked by an architectural
asymmetry — the visual editor is a **stateful** object that draws in **world space** (`set_transform`)
and owns its interaction (`on_down/move/up`, drag members); the audio editor is a **stateless** view
that draws in **screen space** and has its interaction smeared across `input_graph.cpp` (press) +
`frame.cpp` (`update_drag_continuations`, move) + `input.cpp` (release/dispatch) over 13 `Window.ag_*`
fields. Unifying the *draw* first would be ceremony or heavy hooks. So do the interaction unification
first — it removes the asymmetry, after which `GraphCanvas` is a natural lift of two aligned editors.

Target: mirror the visual editor. `App::graph` is a persistent `ui::NodeGraph*`; add a persistent
**`App::audio_graph`** (`ui::AudioNodeGraph*`), give `AudioNodeGraph` interaction **members** (the
current `Window.ag_*` set) + a **member view** (its `zoom_/pan_x_/pan_y_`), and move the gesture
logic out of the four free functions into `on_scroll/on_down/on_move/on_up`. `input.cpp`/`frame.cpp`
then call the one persistent instance instead of rebuilding throwaways.

Sub-steps (each build+test green; the refactor is behavior-preserving; persisting the view is the one
deliberate behavior change):
- **6a** — introduce persistent `App::audio_graph`; `graph_audio_dock`/`graph_scroll`/
  `graph_rewire_release`/`update_drag_continuations`/`session_view` draw all use that instance
  (still stateless-primed) instead of throwaways. Pure plumbing.
- **6b** — move the view (`zoom_/pan_x_/pan_y_`) onto the instance as members; **persist it**
  (mirror `persist.cpp:660` for the visual view). Drop `Window.ag_zoom/ag_pan_*`.
- **6c** — move the drag state (`ag_node_drag`, `ag_wire_from`, `ag_param_*`, `ag_key_*`, `ag_pan*`,
  double-click timers) onto the instance as members.
- **6d** — move the gesture logic into `AudioNodeGraph::on_scroll/on_down/on_move/on_up`; the free
  functions become thin forwarders, then dissolve. Domain edits still route through the session
  C-API + `edit_gateway` exactly as now.
- **6e** — delete the dead `Window.ag_*` fields + the free functions.

Verify after each: audio-graph node select/drag, rewire (audio/note/mod), param knob/slider drag,
key-range drag, add/remove, disconnect, pan/zoom/reset, double-click-editor — all identical.

High regression surface (every audio-graph gesture) → a focused effort with a smoke pass per sub-step.

### Phase 2 acceptance / verification
- Audio graph renders identically (cards, ports, wires, preview, selection); node-drag, rewire,
  pan, zoom behave identically — verified by driving the running audio-graph deep view.
- Native build + 49 tests green; the visual graph is untouched.
- The "MUST match the draw" transform duplication is reduced to one shared derivation.
