# Interface Panel Consolidation Audit

Status: Phase 1 audit  
Date: 2026-07-11  
Scope: native Vivid UI container and panel language

## Purpose

The current interface is close to the intended vivid-classic professional tool direction, but it
uses too many local container treatments at once. The issue is not the palette. The issue is that
panels, cards, menus, dock headers, graph nodes, clip cells, toolbar buttons, modals, and editor
surfaces each draw their own fill, border, accent, and header logic.

This audit reduces the UI to a smaller reusable vocabulary before Phase 2 code cleanup.

## Target Vocabulary

All visible containers should map to one of these roles:

| Role | Use | Drawing rule |
| --- | --- | --- |
| `WorkspaceCanvas` | unframed background behind major zones | `style().bg`, no border, no header |
| `ZonePanel` | top-level bounded regions such as Session, Output, Signal, Sidebar sections | `panel` or `panel_frame`, 1px rule, standard header, domain accent |
| `DetailDock` | bottom focused editor/inspector region | same header language as `ZonePanel`, full-width body, no nested zone frames |
| `ItemBox` | repeated interactive objects such as clips, tracks, menu rows, palette buttons, device buttons | flat `card` fill, 1px outline, left/top accent edge, blue selected frame |
| `Recess` | content wells such as waveforms, thumbnails, meters, sliders, piano roll background | `recess` fill, optional 1px soft border, never a titled panel |
| `OverlayPanel` | modal/chooser/menu shell | `panel`-like shell with shadow/scrim only when modal; rows are `ItemBox` |
| `Separator` | section breaks inside a panel | 1px `border_soft` rule plus `section_header`; no filled sub-panel |

Anything outside this vocabulary should either become one of these roles or be treated as content,
not a container.

## Current Findings

### 1. The shared primitives exist but are not mandatory

`app/src/ui/ui_style.h` already defines the intended language: `panel`, `panel_frame`,
`draw_card`, `section_header`, `slider`, `toggle`, and `dropdown_field`. New UI guidance also says
to keep drawing on those helpers. The current implementation still mixes direct `draw_rect`
container painting with these helpers.

Phase 2 should first add missing primitives, then replace local container painting with them.

### 2. `session_view.cpp` is the main source of container drift

`app/src/ui/session_view.cpp` mixes at least six visual container idioms:

- proper top-level `panel`/`panel_frame` calls for Session, Output, Signal, Clips, and Plugins
- inline menu shells with a manually painted header row
- inline menu rows with a card fill plus a left accent bar
- clip cells with custom body fills, custom title strips, and nested recessed previews
- transport buttons using local rounded-rect calls
- bottom dock header/body chrome that partially duplicates `panel`
- drag and drop overlays using local 2px outline construction

Most of this should collapse into `ZonePanel`, `DetailDock`, `ItemBox`, `Recess`, and `OverlayPanel`.

### 3. The bottom dock should become one `DetailDock`

The bottom region currently paints its own full-width body, resize border, header strip, domain
edge, domain badge, and local buttons. This is visually close to the target, but it is not using the
same primitive as other panels.

Phase 2 should introduce a `detail_dock` or `dock_frame` helper in `ui_style.h` and make the audio
graph, visual node inspector, operator editor, and future focused editors share it.

### 4. Clip cells and track headers need `ItemBox`

Track headers, scene launch buttons, clip cells, mixer buttons, sidebar pool clips, menu rows, graph
palette rows, and output header buttons all repeat the same pattern manually: card fill, hover fill,
accent edge, text. They should use one helper so the interface reads as one system.

The helper needs variants for:

- accent edge orientation: left or top
- selected/queued/active frame
- optional title strip
- optional content well rect
- compact row mode for menus and palette buttons

### 5. Recessed wells need one substrate

There are several variants of "dark inset well":

- clip previews in the session grid and sidebar
- node previews in `node_canvas.h`
- piano roll and waveform backgrounds in `clip_editor.cpp`
- meters and gain sliders in the mixer
- compound widgets in `compound_widget.h`

The graph preview wells are already centralized in `node_preview_panel`, but the broader UI should
have a general `recess` helper with the same fill and optional soft border.

### 6. Graph nodes are mostly coherent but should reuse item vocabulary

`node_canvas.h` already centralizes node cards and preview wells for both audio and visual graphs.
That is good. The remaining mismatch is that nodes use a blue selection block behind the card,
whereas other selected items use local outlines or no outline. Phase 2 should align this with the
global selected `ItemBox` frame rule.

### 7. `clip_editor.cpp` is a standalone editor shell

The clip editor draws its own non-modal panel, top accent, header, roll well, scrollbars, keyboard,
and footer hints. Because it is a rich editor, it can have dense internal drawing, but its shell
should still use the same `ZonePanel`/`DetailDock` header language as other focused editors.

Phase 2 should only normalize the shell and high-level wells. The piano roll grid, notes, waveform,
loop braces, and playhead are content and should remain editor-specific.

### 8. `mapping_overview.cpp` is an overlay with custom chrome

The mappings overview paints a scrim, modal body, top accent, local header, row stripes, stepper
buttons, and polarity chips. It should become an `OverlayPanel` with shared compact `ItemBox`
buttons. Row striping can stay as table content if it uses `border_soft` or a tokenized alternate
row fill.

### 9. Hard-coded colors remain outside `ui_style.h`

Several files use local RGB literals for container fills, selected backgrounds, and wells. Some are
content colors, but many are really style tokens. Phase 2 should move container colors to
`ui_style.h`; content colors can remain local only when they represent musical or visual state.

## Phase 2 Replacement Map

| Current pattern | Primary files | Replacement |
| --- | --- | --- |
| local menu shell header + rows | `session_view.cpp`, `node_graph.cpp` | `overlay_panel` + `item_box_row` |
| transport rounded button backgrounds | `session_view.cpp` | `icon_button`/`toolbar_button` using hard corners |
| track headers, add track, scene launch | `session_view.cpp` | `item_box` |
| clip cell body + title strip + preview well | `session_view.cpp` | `clip_item_box` built from `item_box` + `recess` |
| mixer ARM/VIZ buttons | `session_view.cpp` | compact `item_box_button` |
| bottom dock frame/header | `session_view.cpp` | `detail_dock` |
| popout/graph/editor/float header buttons | `session_view.cpp` | compact `item_box_button` |
| node card selection block | `node_canvas.h` | selected `item_box` frame behavior |
| node preview well | `node_canvas.h` | general `recess` helper, preserving graph-specific wrapper if useful |
| clip editor shell | `clip_editor.cpp` | `editor_panel`/`detail_dock` shell |
| mapping overview modal | `mapping_overview.cpp` | `overlay_panel` + compact item buttons |
| compound widget dark backgrounds | `compound_widget.h` | `recess` helper |

## Phase 2 Acceptance Criteria

- All top-level regions use one shared panel/header implementation.
- The bottom dock uses the same header and domain accent rules as other regions.
- Repeated interactive objects use a shared `ItemBox` helper instead of local card/accent drawing.
- Recessed content wells use one helper and one tokenized fill/border.
- Menus and modals use one overlay shell.
- Transport controls are hard-corner tool controls, not rounded local widgets.
- Container nesting stays shallow: workspace canvas, zone/detail panel, item/recess content.
- No new hard-coded container colors are introduced outside `ui_style.h`.
- Existing graph/editor content drawing remains intact unless it is shell/container chrome.

## Suggested Phase 2 Order

1. Add shared helpers to `ui_style.h`: `separator`, `item_box`, `recess`, `overlay_panel`,
   `toolbar_button`, and `detail_dock`.
2. Convert the smallest repeated surfaces first: menus, header buttons, graph palette buttons,
   mixer ARM/VIZ buttons.
3. Convert the session grid: track headers, scene launch buttons, add-track cell, clip cells.
4. Convert the bottom dock shell.
5. Normalize overlay shells and the clip editor shell.
6. Run the native tests and capture before/after screenshots for visual review.
