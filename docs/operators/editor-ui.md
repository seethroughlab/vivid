# Editor UI for operator authors

This doc covers the `VIVID_EDITOR` API as of the editor-UI platform
plan: what the paint, widget, and host-service layers give you, how to
compose them inside `draw_editor(...)`, and where operator-specific
state should live. Reference adopters are `operators/control/
drum_sequencer/drum_sequencer_editor.cpp` (side panel + grid) and
`operators/control/mseg/mseg_editor.cpp` (plot + drag handles).

## The three layers

```
operator::draw_editor(ctx)
        │
        ▼
  editor_ui.h           ← interactive widgets + layout helpers
        │                  (src/operator_api/editor_ui.h)
        ▼
  draw_ui_helpers.h     ← stateless render-only helpers
        │                  (src/operator_api/draw_ui_helpers.h)
        ▼
  VividDrawAPI fn ptrs  ← the paint surface (ctx.draw.*)
```

- **`VividDrawAPI`** — `ctx.draw.draw_rect`, `draw_rounded_rect`,
  `draw_text`, `draw_line`, `draw_tri`, `draw_arc`, `draw_text_wrapped`,
  `text_width`, `line_height`, `push_clip_rect`, `pop_clip_rect`. The
  paint primitives. Always available; operators use them directly for
  bespoke visuals (curve polylines, playheads, custom glyphs).
- **`draw_ui_helpers.h`** — `vivid::draw_ui::draw_panel`,
  `draw_button`, `draw_meter`, `draw_labeled_slider_readonly`,
  `draw_grid_cell`, etc. Stateless render-only helpers. Use these
  when you want a "looks like the rest of Vivid" panel/button visual
  without writing your own rectangle math.
- **`editor_ui.h`** — `vivid::ui::ui_button`, `ui_toggle`, `ui_radio`,
  `ui_slider_h`, `ui_step_grid`, `ui_drag_handle`, `ui_scroll_region_*`
  plus `LayoutCursor` (`ui_layout`, `ui_row`, `ui_column`, `ui_split_h`,
  `ui_pad`). These are the **interactive** widgets: each one consumes
  `ctx.mouse` + `ctx.events` and returns a small result struct. They
  render themselves through `draw_ui_helpers.h`, so visual style stays
  consistent.

Rule of thumb: **reach for a widget first**; drop down to
`draw_ui_helpers` for a stateless panel that isn't a known widget; drop
all the way to `ctx.draw.*` only for custom visuals that a widget
doesn't cover.

## Cheatsheet

### Widgets (`vivid::ui::` in `src/operator_api/editor_ui.h`)

| You need… | Call | State struct | Result highlights |
|---|---|---|---|
| Click action | `ui_button(ctx, r, label)` | — | `.clicked` |
| On/off state | `ui_toggle(ctx, r, label, cur)` | — | `.clicked`, `.value` |
| Mutually-exclusive set (≤4) | `ui_radio(ctx, r, labels, n, cur)` | — | `.clicked`, `.value` |
| Horizontal slider | `ui_slider_h(ctx, r, label, v, lo, hi, &st)` | `SliderState` | `.changed`, `.value` |
| Vertical slider / meter | `ui_slider_v(ctx, r, v, lo, hi, &st)` | `SliderState` | `.changed`, `.value` |
| Grid cell click/drag/shift-extend | `ui_step_grid(ctx, bounds, rows, cols, active_cols, &st)` | `GridState` | `.cell_clicked`, `.drag_painting`, `.shift_extending` |
| Draggable point on a plane | `ui_drag_handle(ctx, cx, cy, radius, &st)` | `DragHandleState` | `.dragging`, `.dx`, `.dy` |
| Scrollable content region | `ui_scroll_region_begin/end(ctx, bounds, content_h, &st)` | `ScrollState` | — |
| Single-line text entry (ASCII) | `ui_text_field(ctx, r, buf, buflen, &st, placeholder)` | `TextFieldState` | `.committed`, `.changed`, `.cancelled`, `.focused` |

All widget state structs are caller-owned — operators typically keep one per widget instance on their core struct.

### Layout (`vivid::ui::`)

| You need… | Call |
|---|---|
| Seed a layout cursor over a rect | `ui_layout(Rect, pad, gap)` → `LayoutCursor` |
| Carve a full-width row | `ui_row(cursor, height)` → `Rect` |
| Carve a full-height column | `ui_column(cursor, width)` → `Rect` |
| Split left/right at fraction | `ui_split_h(rect, fraction, gap)` → `{left, right}` |
| Split top/bottom at fraction | `ui_split_v(rect, fraction, gap)` → `{top, bottom}` |
| Shrink a rect by inset | `ui_pad(rect, inset)` → `Rect` |
| Per-cell rect inside a grid | `grid_cell_rect(bounds, rows, cols, row, col)` → `Rect` |

### Selection (`vivid::editor_ui::` in `operators/shared/editor_ui/selection.h`)

Shared rectangular-selection geometry reused across grid editors.

| You need… | Call |
|---|---|
| Point selection | `selection_from_point(row, col)` → `Selection` |
| Rect from anchor + tip | `selection_from_anchor_tip(a_row, a_col, t_row, t_col)` |
| Grow rect to include a cell | `selection_extend(sel, row, col)` |
| Test membership | `selection_contains(sel, row, col)` |
| Cell count (point = 1) | `selection_cell_count(sel)` |
| Clamped cursor move | `cursor_move(dx, dy, max_row, max_col, &cur_r, &cur_c)` |
| Clamp cursor + anchor + rect after a bounds shrink | `clamp_editor_state(max_row, max_col, &cur_r, &cur_c, &anc_r, &anc_c, &sel)` |

`Selection` members are `row_lo/hi`, `col_lo/hi` — map your axes onto row/col (DrumSequencer uses row=drum; Sequencer uses row={value,gate}).

### Keyboard (`vivid::editor_keys::` in `src/operator_api/editor_keys.h`)

Mirrors the GLFW keycodes you care about without linking GLFW.

| Category | Constants / helpers |
|---|---|
| Event action | `kPress`, `kRelease`, `kRepeat` |
| Modifiers | `kModShift`, `kModControl`, `kModAlt`, `kModSuper` |
| Printable | `kSpace`, `k0..k9`, `kA..kZ`, `kApostrophe`, `kComma`, `kMinus`, `kPeriod`, `kSlash` |
| Navigation / editing | `kEscape`, `kEnter`, `kTab`, `kBackspace`, `kDelete`, `kLeft/Right/Up/Down`, `kHome/End`, `kPageUp/Down` |
| Helpers | `is_cmd_or_ctrl(modifiers)`, `is_digit_key(key)`, `digit_value(key)`, `is_letter_key(key)` |

Typical event-loop shape:

```cpp
namespace ek = ::vivid::editor_keys;
for (uint32_t i = 0; i < ctx->event_count; ++i) {
    const auto& e = ctx->events[i];
    if (e.type != VIVID_EDITOR_EVENT_KEY) continue;
    if (e.action != ek::kPress && e.action != ek::kRepeat) continue;
    const bool shift       = (e.modifiers & ek::kModShift) != 0;
    const bool cmd_or_ctrl = ek::is_cmd_or_ctrl(e.modifiers);
    if (e.key == ek::kEnter)           { /* commit */ }
    else if (ek::is_digit_key(e.key))  { /* digit */ }
    // …
}
```

## Caller-owned state

Widgets that need cross-frame context (drag origin, grid anchor,
scroll position) take a pointer to a **caller-owned** state struct.
Operators typically declare one per widget instance on their core:

```cpp
// DrumSequencerCore members.
vivid::ui::SliderState sp_vel_drag_{};
vivid::ui::SliderState sp_modb_drag_{};
vivid::ui::SliderState sp_prob_drag_{};
vivid::ui::GridState   grid_state_{};
```

```cpp
// MSEG members.
std::array<vivid::ui::DragHandleState, kMaxPoints> point_drag_{};
std::array<vivid::ui::DragHandleState, kMaxCurves> curve_drag_{};
```

The toolkit never allocates, never holds a registry, and never reaches
into operator state. If the operator is torn down (hot reload, node
removal), its state structs go with it; next frame the widgets start
from zeros.

## Widgets never call `set_param`

Widgets **only** report outcomes; the caller decides whether to emit a
command. This preserves the one-writer invariant — every param mutation
lands via `ctx.commands.set_param` in one obvious place in the editor,
which keeps undo history clean and avoids hidden writes during layout.

```cpp
auto r = vivid::ui::ui_slider_h(*ctx, slot, "Vel",
    cur_vel, 0.0f, 1.0f, &sp_vel_drag_);
if (r.changed) set_velocity_selection(r.value);   // operator chooses
```

## Layout cookbook

```cpp
// Typical editor frame: top bar + grid + right side panel.
auto root = vivid::ui::ui_layout(
    vivid::ui::Rect{0, 0, ctx->surface_width, ctx->surface_height},
    /*pad=*/8.0f, /*gap=*/4.0f);
const auto top_bar = vivid::ui::ui_row(root, 26.0f);
const auto body    = vivid::ui::ui_row(root, root.remaining_h);
const auto [grid, sidebar] =
    vivid::ui::ui_split_h(body, 0.7f, /*gap=*/8.0f);

// Draw into top_bar, grid, sidebar rects. Widgets accept a Rect; the
// result is always "this rect is yours; everything else stayed in the
// cursor" — no retained state, no invisible allocation.
```

## Host services (`ctx.host`)

Clipboard, cursor shape, pointer capture, focus, status strip, tooltip —
every callback is optional. Guard before calling:

```cpp
if (ctx->host.set_cursor)
    ctx->host.set_cursor(ctx->host.opaque, VIVID_CURSOR_HAND);

if (ctx->host.set_status_text)
    ctx->host.set_status_text(ctx->host.opaque, "hat · step 5");

if (ctx->host.set_clipboard_text)
    ctx->host.set_clipboard_text(ctx->host.opaque, json_blob.c_str());
```

Cursor, status text, and tooltip reset to defaults every frame; the
operator re-declares what it wants each tick. Pointer-capture and
focus-request are persistent until explicitly cleared / granted.

Clipboard reads return a stable `const char*` that's valid until the
next host callback on the same window. Copy if you need persistence
past a single call.

`VIVID_CURSOR_*` kinds: `DEFAULT`, `ARROW`, `IBEAM`, `CROSSHAIR`,
`HAND`, `RESIZE_H`, `RESIZE_V`, `RESIZE_NESW`, `RESIZE_NWSE`.

## Event loop shape

Inside `draw_editor(*ctx)`, a typical operator does:

1. Drain `ctx->events` for keyboard + scroll (widgets consume mouse on
   their own via `ctx->mouse`).
2. Resolve live param state (you can still read `ctx->param_values`).
3. Compute layout via `ui_layout` / `ui_row` / etc.
4. Call widgets in rendering order. Top-bar before grid before side
   panel is typical — keeps occluding overlays obvious.
5. Inspect widget result structs; emit `ctx->commands.set_param`
   as needed.
6. Do any custom drawing that widgets can't cover (curve polylines,
   playhead, selection outlines — `drum_sequencer_editor.cpp` draws the
   per-cell glyphs this way after `ui_step_grid` reports its click /
   drag / shift-extend events).
7. Optionally set cursor / status / tooltip via `ctx.host.*`.

## Out of scope (follow-ups)

- Editable text entry + IME
- Native file dialogs
- Vector primitives beyond the Phase A addition
  (`draw_tri` / `draw_arc` / `draw_text_wrapped`). A future plan can
  add `draw_circle` / `draw_polyline` / transform stack if an adopter
  needs them.
- Retained UI framework, style theming system. The toolkit stays
  immediate-mode and operator-owned.

## Anchors

- `src/operator_api/editor_ui.h` — widget toolkit.
- `src/operator_api/draw_ui_helpers.h` — stateless render helpers.
- `src/operator_api/draw_plot_helpers.h` — stateless plot-style renders
  (waveform, envelope, playhead line, scope meters).
- `src/operator_api/editor_keys.h` — GLFW key + modifier constants
  (mirrored values) and the `is_cmd_or_ctrl` / `is_digit_key` helpers.
- `operators/shared/editor_ui/selection.h` — shared `Selection` rect,
  `cursor_move`, `clamp_editor_state` for grid editors.
- `src/operator_api/types.h` — `VividEditorContext`,
  `VividEditorHostAPI`, `VividCursorKind`.
- `src/runtime/core/editor_window_manager.cpp` — host-side
  `VividEditorContext` assembly + cursor cache + status / tooltip
  renderer.
- `src/runtime/core/editor_window_host_api.{h,cpp}` — the HostCtx
  thunks behind `ctx.host`.
- `operators/control/drum_sequencer/drum_sequencer_editor.cpp` —
  reference adopter: side panel (sliders + toggles + radio), unified
  grid with `ui_step_grid`, cursor + status via `ctx.host.*`.
- `operators/control/mseg/mseg_editor.cpp` — reference adopter: drag
  handles via `ui_drag_handle_begin` / `ui_drag_handle_update` paired
  with the operator's own nearest-hit picker.
