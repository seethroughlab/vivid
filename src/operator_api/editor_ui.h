#pragma once
//
// Editor UI toolkit — immediate-mode widgets + layout helpers for
// VIVID_EDITOR windows. Sits on top of the stateless rendering
// helpers in draw_ui_helpers.h and consumes VividEditorContext for
// input + drawing.
//
// Design contract:
//   * Header-only, operator-friendly (no new link deps).
//   * Immediate-mode. Widgets are called once per frame; there is no
//     retained tree.
//   * Caller-owned state. Widgets that need cross-frame bookkeeping
//     (e.g. drag origin) take a pointer to a caller-allocated struct
//     (`SliderState`, `GridState`, `DragHandleState`, `ScrollState`).
//     Operators typically store one such struct per widget instance
//     on their core type.
//   * Widgets never call `ctx.commands.set_param`. They return small
//     result structs; the operator decides whether to emit a command.
//     Preserves the one-writer invariant (see docs/operators/editor-ui.md).
//   * Rendering is delegated to `draw_ui_helpers.h` wherever possible.

#include "operator_api/types.h"
#include "operator_api/draw_ui_helpers.h"
#include "operator_api/editor_keys.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

namespace vivid::ui {

// ---------------------------------------------------------------------------
// Basic types
// ---------------------------------------------------------------------------

// Half-open axis-aligned rectangle in editor-window pixel space.
struct Rect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    bool contains(float px, float py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

// Per-slider cross-frame state — caller owns one per slider instance.
struct SliderState {
    bool  dragging        = false;
    float drag_start_v    = 0.0f;  // value when drag began
    float drag_start_mx   = 0.0f;  // cursor X (or Y for vertical) at drag start
};

// Widget result structs.  Callers read the fields they care about; the
// rest carry documented defaults so `if (r.clicked)` is always valid.
struct ButtonResult {
    bool hovered = false;
    bool pressed = false;   // cursor in rect AND left button currently held
    bool clicked = false;   // left_clicked landed this frame in the rect
};

struct ToggleResult {
    bool clicked = false;   // user pressed this frame — caller flips state
    bool value   = false;   // current logical state (echoed unchanged if !clicked)
};

struct RadioResult {
    bool clicked = false;
    int  value   = -1;      // index of active option (echoed unchanged if !clicked)
};

struct SliderResult {
    bool  changed  = false; // value differs from input this frame
    bool  dragging = false; // drag in progress (caller can use for UX hints)
    float value    = 0.0f;  // value to emit if `changed` — caller may ignore
};

// ---------------------------------------------------------------------------
// Layout cursor
// ---------------------------------------------------------------------------
//
// Operators typically call ui_layout(...) once per frame to get a cursor
// seeded from the editor's surface_width × surface_height (or a sub-rect
// like the side panel bounds), then walk it with ui_row / ui_column to
// carve successive widget slots.

struct LayoutCursor {
    Rect bounds;                  // the outer region being consumed
    float pad = 0.0f;             // inner padding (subtracted on construction)
    float gap = 4.0f;             // default spacing between rows / columns

    float cursor_x = 0.0f;        // top-left X of the next row / column
    float cursor_y = 0.0f;        // top-left Y of the next row / column
    float remaining_w = 0.0f;     // width still available for rows
    float remaining_h = 0.0f;     // height still available for columns
};

inline LayoutCursor ui_layout(Rect outer, float pad = 0.0f, float gap = 4.0f) {
    LayoutCursor c{};
    c.bounds    = outer;
    c.pad       = pad;
    c.gap       = gap;
    c.cursor_x  = outer.x + pad;
    c.cursor_y  = outer.y + pad;
    c.remaining_w = std::max(0.0f, outer.w - 2.0f * pad);
    c.remaining_h = std::max(0.0f, outer.h - 2.0f * pad);
    return c;
}

// Consume a full-width horizontal row of the given height; advance cursor_y.
inline Rect ui_row(LayoutCursor& c, float height, float override_gap = -1.0f) {
    const float gap = (override_gap >= 0.0f) ? override_gap : c.gap;
    const float actual_h = std::min(height, c.remaining_h);
    Rect r{c.cursor_x, c.cursor_y, c.remaining_w, actual_h};
    const float consumed = actual_h + gap;
    c.cursor_y   += consumed;
    c.remaining_h = std::max(0.0f, c.remaining_h - consumed);
    return r;
}

// Consume a full-height vertical column of the given width; advance cursor_x.
inline Rect ui_column(LayoutCursor& c, float width, float override_gap = -1.0f) {
    const float gap = (override_gap >= 0.0f) ? override_gap : c.gap;
    const float actual_w = std::min(width, c.remaining_w);
    Rect r{c.cursor_x, c.cursor_y, actual_w, c.remaining_h};
    const float consumed = actual_w + gap;
    c.cursor_x   += consumed;
    c.remaining_w = std::max(0.0f, c.remaining_w - consumed);
    return r;
}

inline Rect ui_pad(Rect r, float inset) {
    return Rect{
        r.x + inset, r.y + inset,
        std::max(0.0f, r.w - 2.0f * inset),
        std::max(0.0f, r.h - 2.0f * inset)
    };
}

// Split a rect into {left, right} at `fraction` (0..1) of its width,
// optionally leaving `gap` pixels of dead space between halves.
inline std::pair<Rect, Rect> ui_split_h(Rect r, float fraction, float gap = 0.0f) {
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    const float available = std::max(0.0f, r.w - gap);
    const float lw = available * fraction;
    const float rw = available - lw;
    return {
        Rect{r.x, r.y, lw, r.h},
        Rect{r.x + lw + gap, r.y, rw, r.h}
    };
}

// Split a rect into {top, bottom}.
inline std::pair<Rect, Rect> ui_split_v(Rect r, float fraction, float gap = 0.0f) {
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    const float available = std::max(0.0f, r.h - gap);
    const float th = available * fraction;
    const float bh = available - th;
    return {
        Rect{r.x, r.y, r.w, th},
        Rect{r.x, r.y + th + gap, r.w, bh}
    };
}

// ---------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------

namespace detail {

inline VividColor accent_color(const VividInspectorTheme& th, float alpha = 1.0f) {
    return VividColor{th.accent.r, th.accent.g, th.accent.b, alpha};
}
inline VividColor bright_text_color(const VividInspectorTheme& th, float alpha = 1.0f) {
    return VividColor{th.bright_text.r, th.bright_text.g, th.bright_text.b, alpha};
}
inline VividColor dim_text_color(const VividInspectorTheme& th, float alpha = 0.95f) {
    return VividColor{th.dim_text.r, th.dim_text.g, th.dim_text.b, alpha};
}

// The default fill used when an operator hasn't passed an explicit colour.
// Chosen to sit against theme.dark_bg without fighting the theme.accent
// used for "on" states.
inline VividColor default_button_fill_off() {
    return VividColor{0.20f, 0.20f, 0.23f, 1.0f};
}

// Meter fill + track used by slider widgets. Matches the Phase-4
// drum-sequencer side panel so existing editors render identically.
inline VividColor default_meter_fill() {
    return VividColor{0.9f, 0.7f, 0.2f, 0.9f};
}
inline VividColor default_meter_track() {
    return VividColor{0.15f, 0.15f, 0.17f, 0.9f};
}

// Map mouse X to [0,1] across a labeled_slider_readonly meter. Geometry
// must stay aligned with draw_ui::draw_labeled_slider_readonly so the
// drag cursor hits the visible fill exactly.
inline float labeled_slider_frac(Rect r, float mx) {
    constexpr float kLabelFrac = 0.35f;
    constexpr float kValueFrac = 0.15f;
    const float meter_x = r.x + r.w * kLabelFrac + 4.0f;
    const float meter_w = std::max(1.0f,
        r.w * (1.0f - kLabelFrac - kValueFrac) - 8.0f);
    return std::clamp((mx - meter_x) / meter_w, 0.0f, 1.0f);
}

} // namespace detail

// --- Introspection -------------------------------------------------------
//
// When the host installs an introspection sink on `ctx`, each widget emits
// one VividIntrospectWidget record describing its bounds + live state so
// LLM / test tooling can read the editor's structure without OCR'ing pixels.
// When the sink is null (the common case), every widget's emit is a
// cheap null check.
inline void introspect_emit(const VividEditorContext& ctx,
                            const VividIntrospectWidget& w) {
    if (ctx.introspect_fn) ctx.introspect_fn(ctx.introspect_sink, &w);
}

// A labeled push button. Reports hover/press/click; the caller decides what
// happens. `active` controls the render-as-pressed visual state only —
// callers typically pass the logical state of the feature the button
// represents (e.g. `cur_trig_a`).
inline ButtonResult ui_button(VividEditorContext& ctx, Rect r,
                              const char* label, bool active = false,
                              VividColor fill_off = {},
                              VividColor fill_on  = {}) {
    const auto& mouse = ctx.mouse;
    ButtonResult out;
    out.hovered = r.contains(mouse.x, mouse.y);
    out.pressed = out.hovered && mouse.left_down != 0;
    out.clicked = out.hovered && mouse.left_clicked != 0;

    const VividColor off = (fill_off.a > 0.0f)
        ? fill_off : detail::default_button_fill_off();
    const VividColor on  = (fill_on.a > 0.0f)
        ? fill_on : detail::accent_color(ctx.theme, 1.0f);
    const VividColor text_col = detail::bright_text_color(ctx.theme, 1.0f);

    vivid::draw_ui::draw_button(ctx.draw, ctx.draw.opaque,
        r.x, r.y, r.w, r.h, label ? label : "", active,
        off, on, text_col);

    VividIntrospectWidget iw{};
    iw.kind  = "button";
    iw.x = r.x; iw.y = r.y; iw.w = r.w; iw.h = r.h;
    iw.label = label;
    if (active)      iw.flags |= VIVID_INTROSPECT_ACTIVE;
    if (out.hovered) iw.flags |= VIVID_INTROSPECT_HOVERED;
    if (out.pressed) iw.flags |= VIVID_INTROSPECT_PRESSED;
    if (out.clicked) iw.flags |= VIVID_INTROSPECT_CHANGED;
    introspect_emit(ctx, iw);
    return out;
}

// A two-state toggle. `clicked` indicates the user flipped state this
// frame; `value` is the resulting logical state.
inline ToggleResult ui_toggle(VividEditorContext& ctx, Rect r,
                              const char* label, bool current,
                              VividColor fill_off = {},
                              VividColor fill_on  = {}) {
    ToggleResult out;
    out.value = current;
    // ui_button already emits a "button" record — for a toggle we also
    // emit a semantically-richer "toggle" record that exposes the
    // current value as a flag so LLM tooling can distinguish a
    // momentary button from a bistable control.
    const ButtonResult btn = ui_button(ctx, r, label, current, fill_off, fill_on);
    if (btn.clicked) {
        out.clicked = true;
        out.value = !current;
    }
    VividIntrospectWidget iw{};
    iw.kind  = "toggle";
    iw.x = r.x; iw.y = r.y; iw.w = r.w; iw.h = r.h;
    iw.label = label;
    if (out.value)   iw.flags |= VIVID_INTROSPECT_VALUE;
    if (out.clicked) iw.flags |= VIVID_INTROSPECT_CHANGED;
    introspect_emit(ctx, iw);
    return out;
}

// Radio / segmented control. Lays out `count` equal-width buttons across
// `r` and returns the clicked index (or -1 if none).
inline RadioResult ui_radio(VividEditorContext& ctx, Rect r,
                            const char* const* labels, int count, int current,
                            VividColor fill_off = {},
                            VividColor fill_on  = {}) {
    RadioResult out;
    out.value = current;
    if (!labels || count <= 0 || r.w <= 0.0f) return out;
    const float step_w = r.w / static_cast<float>(count);
    for (int i = 0; i < count; ++i) {
        Rect cell{r.x + step_w * static_cast<float>(i), r.y, step_w, r.h};
        const ButtonResult btn = ui_button(ctx, cell,
            labels[i] ? labels[i] : "", i == current,
            fill_off, fill_on);
        if (btn.clicked) {
            out.clicked = true;
            out.value = i;
        }
    }
    // Radio-level record (alongside per-cell "button" records) so LLM
    // tooling can address the group by index rather than cell bounds.
    VividIntrospectWidget iw{};
    iw.kind = "radio";
    iw.x = r.x; iw.y = r.y; iw.w = r.w; iw.h = r.h;
    iw.int_value = out.value;
    iw.cols      = count;
    if (current >= 0 && current < count && labels && labels[current])
        iw.label = labels[current];
    if (out.clicked) iw.flags |= VIVID_INTROSPECT_CHANGED;
    introspect_emit(ctx, iw);
    return out;
}

// Horizontal slider (label on left, fill track in centre, numeric readout
// on right). Drag begins when the user left-clicks anywhere inside the
// rect and ends on left-release; the widget writes the drag flag into
// `*state` and reports a candidate value every frame the user is
// dragging.  Caller decides whether to emit a set_param on `changed`.
inline SliderResult ui_slider_h(VividEditorContext& ctx, Rect r,
                                const char* label,
                                float value, float lo, float hi,
                                SliderState* state,
                                VividColor fill_color = {},
                                VividColor track_color = {}) {
    SliderResult out;
    out.value = value;
    const auto& mouse = ctx.mouse;

    if (state) {
        if (mouse.left_clicked && r.contains(mouse.x, mouse.y)) {
            state->dragging = true;
            state->drag_start_v = value;
            state->drag_start_mx = mouse.x;
        }
        if (state->dragging && mouse.left_released) {
            state->dragging = false;
        }
        if (state->dragging) {
            const float frac = detail::labeled_slider_frac(r, mouse.x);
            const float span = hi - lo;
            const float new_val = lo + frac * span;
            if (new_val != value) {
                out.changed = true;
                out.value = new_val;
            }
        }
        out.dragging = state->dragging;
    }

    // Render with the (potentially updated) value so visual feedback is
    // immediate even when the caller ignores `changed` this frame.
    const VividColor fill  = (fill_color.a  > 0.0f) ? fill_color
                                                    : detail::default_meter_fill();
    const VividColor track = (track_color.a > 0.0f) ? track_color
                                                    : detail::default_meter_track();
    const VividColor label_col = detail::bright_text_color(ctx.theme, 0.95f);
    const VividColor value_col = detail::dim_text_color(ctx.theme, 0.95f);

    vivid::draw_ui::draw_labeled_slider_readonly(
        ctx.draw, ctx.draw.opaque,
        r.x, r.y, r.w, r.h, label ? label : "",
        out.value, lo, hi,
        label_col, value_col, fill, track);

    VividIntrospectWidget iw{};
    iw.kind     = "slider_h";
    iw.x = r.x; iw.y = r.y; iw.w = r.w; iw.h = r.h;
    iw.label    = label;
    iw.value    = out.value;
    iw.value_lo = lo;
    iw.value_hi = hi;
    if (out.changed)  iw.flags |= VIVID_INTROSPECT_CHANGED;
    if (out.dragging) iw.flags |= VIVID_INTROSPECT_DRAGGING;
    introspect_emit(ctx, iw);
    return out;
}

// Vertical slider — renders a meter filling bottom→top; drag in Y. The
// visual is a plain meter (no label text); callers that need a labeled
// layout should compose with their own `draw_text` call above the rect.
inline SliderResult ui_slider_v(VividEditorContext& ctx, Rect r,
                                float value, float lo, float hi,
                                SliderState* state,
                                VividColor fill_color = {},
                                VividColor track_color = {}) {
    SliderResult out;
    out.value = value;
    const auto& mouse = ctx.mouse;

    if (state) {
        if (mouse.left_clicked && r.contains(mouse.x, mouse.y)) {
            state->dragging = true;
            state->drag_start_v = value;
            state->drag_start_mx = mouse.y;  // field reused for Y
        }
        if (state->dragging && mouse.left_released) {
            state->dragging = false;
        }
        if (state->dragging && r.h > 0.0f) {
            const float frac = std::clamp((mouse.y - r.y) / r.h, 0.0f, 1.0f);
            // y increases downward; flip so top == max.
            const float new_val = hi + (lo - hi) * frac;
            if (new_val != value) {
                out.changed = true;
                out.value = new_val;
            }
        }
        out.dragging = state->dragging;
    }

    const VividColor fill  = (fill_color.a  > 0.0f) ? fill_color
                                                    : detail::default_meter_fill();
    const VividColor track = (track_color.a > 0.0f) ? track_color
                                                    : detail::default_meter_track();
    const float span = std::max(1e-6f, hi - lo);
    const float fill_frac = std::clamp((out.value - lo) / span, 0.0f, 1.0f);
    vivid::draw_ui::draw_meter(ctx.draw, ctx.draw.opaque,
        r.x, r.y, r.w, r.h,
        fill_frac, fill, track,
        vivid::draw_ui::MeterOrientation::Vertical, 2.0f);

    VividIntrospectWidget iw{};
    iw.kind     = "slider_v";
    iw.x = r.x; iw.y = r.y; iw.w = r.w; iw.h = r.h;
    iw.value    = out.value;
    iw.value_lo = lo;
    iw.value_hi = hi;
    if (out.changed)  iw.flags |= VIVID_INTROSPECT_CHANGED;
    if (out.dragging) iw.flags |= VIVID_INTROSPECT_DRAGGING;
    introspect_emit(ctx, iw);
    return out;
}

// ---------------------------------------------------------------------------
// Step grid — shared click / shift-extend / drag-paint interaction for any
// row-by-column cell grid. Rendering stays entirely in the caller (per-cell
// glyphs, column highlights, selection outlines are all operator-specific).
// The widget only converts raw mouse into structured events + per-frame state.
// ---------------------------------------------------------------------------

// Per-grid cross-frame state — caller owns one.  Most fields are managed
// internally by the widget; callers read `anchor_row/col` when rebuilding
// their selection rect after a shift-extend.
struct GridState {
    int anchor_row = 0;
    int anchor_col = 0;

    // Exactly one of these may be true during an active drag.
    bool drag_painting   = false;  // plain drag from a no-shift click
    bool drag_extending  = false;  // shift+drag selection extend

    // For drag_painting: the cell the click originally landed on. Stable
    // across all drag frames until release.
    int drag_row = -1;
    int drag_col = -1;
};

struct GridResult {
    // Click landed on a cell this frame (with or without shift).
    bool cell_clicked         = false;
    int  clicked_row          = -1;
    int  clicked_col          = -1;
    bool clicked_with_shift   = false;

    // The caller should paint at (drag_row, drag_col), using
    // drag_mouse_y_in_cell ∈ [0, 1] (0 at the cell's top edge, 1 at the
    // bottom). Fires on the click frame and every subsequent drag frame
    // until release.
    bool  drag_painting          = false;
    int   drag_row               = -1;
    int   drag_col               = -1;
    float drag_mouse_y_in_cell   = 0.0f;

    // Shift-extending: tip cell tracks the cursor; caller rebuilds its
    // selection rect from state->anchor_row/col to (tip_row, tip_col).
    // Fires on shift-click and every subsequent shift-drag frame where
    // the cursor lands in a valid cell.
    bool shift_extending = false;
    int  tip_row = -1;
    int  tip_col = -1;
};

// Geometry helper — returns the Rect for cell (row, col) inside `bounds`.
// The grid fills bounds uniformly; callers that want a label gutter should
// shrink `bounds` beforehand via ui_pad / ui_split_h.
inline Rect grid_cell_rect(Rect bounds, int rows, int cols, int row, int col) {
    if (rows <= 0 || cols <= 0) return Rect{};
    const float cw = bounds.w / static_cast<float>(cols);
    const float ch = bounds.h / static_cast<float>(rows);
    return Rect{
        bounds.x + cw * static_cast<float>(col),
        bounds.y + ch * static_cast<float>(row),
        cw, ch
    };
}

namespace detail {

// Inverse of grid_cell_rect. Returns (row=-1, col=-1) when the point is
// outside the grid bounds or past `active_cols`. Matches cell_from_mouse
// semantics in drum_sequencer_editor_shared without depending on it.
struct CellCoords { int row = -1, col = -1; };
inline CellCoords grid_cell_from_mouse(Rect bounds, int rows, int cols,
                                       int active_cols,
                                       float mx, float my) {
    CellCoords out;
    if (rows <= 0 || cols <= 0) return out;
    if (bounds.w <= 0.0f || bounds.h <= 0.0f) return out;
    if (mx < bounds.x || mx >= bounds.x + bounds.w) return out;
    if (my < bounds.y || my >= bounds.y + bounds.h) return out;
    const float cw = bounds.w / static_cast<float>(cols);
    const float ch = bounds.h / static_cast<float>(rows);
    const int col = static_cast<int>((mx - bounds.x) / cw);
    const int row = static_cast<int>((my - bounds.y) / ch);
    if (col < 0 || col >= cols) return out;
    if (row < 0 || row >= rows) return out;
    if (active_cols >= 0 && col >= active_cols) return out;
    out.row = row;
    out.col = col;
    return out;
}

} // namespace detail

// Widget. Pure-input: no rendering happens here. Caller iterates cells
// with grid_cell_rect for drawing.
//
// `active_cols` limits hit-testing to the first N columns (drum sequencer
// uses this for `num_steps`, so inactive trailing cells are non-interactive
// even though the grid region spans them visually). Pass -1 to leave all
// columns live.
inline GridResult ui_step_grid(VividEditorContext& ctx, Rect bounds,
                               int rows, int cols, int active_cols,
                               GridState* state) {
    GridResult out;
    if (!state) return out;
    const auto& mouse = ctx.mouse;
    const bool shift = mouse.shift_down != 0;

    const auto hit = detail::grid_cell_from_mouse(
        bounds, rows, cols, active_cols, mouse.x, mouse.y);

    // ---- Click frame (new press) ----
    if (mouse.left_clicked && hit.row >= 0) {
        out.cell_clicked = true;
        out.clicked_row  = hit.row;
        out.clicked_col  = hit.col;
        out.clicked_with_shift = shift;

        if (shift) {
            state->drag_extending = true;
            state->drag_painting  = false;
            out.shift_extending = true;
            out.tip_row = hit.row;
            out.tip_col = hit.col;
        } else {
            // New anchor on plain click; start drag-paint.
            state->anchor_row = hit.row;
            state->anchor_col = hit.col;
            state->drag_painting = true;
            state->drag_extending = false;
            state->drag_row = hit.row;
            state->drag_col = hit.col;

            out.drag_painting = true;
            out.drag_row = hit.row;
            out.drag_col = hit.col;
            const Rect cr = grid_cell_rect(bounds, rows, cols, hit.row, hit.col);
            out.drag_mouse_y_in_cell = (cr.h > 0.0f)
                ? std::clamp((mouse.y - cr.y) / cr.h, 0.0f, 1.0f) : 0.0f;
        }
    }
    // ---- Drag continuation (no new press, but button still held) ----
    else if (mouse.left_down && !mouse.left_clicked) {
        if (state->drag_painting &&
            state->drag_row >= 0 && state->drag_col >= 0) {
            out.drag_painting = true;
            out.drag_row = state->drag_row;
            out.drag_col = state->drag_col;
            const Rect cr = grid_cell_rect(bounds, rows, cols,
                                           state->drag_row, state->drag_col);
            out.drag_mouse_y_in_cell = (cr.h > 0.0f)
                ? std::clamp((mouse.y - cr.y) / cr.h, 0.0f, 1.0f) : 0.0f;
        } else if (state->drag_extending && hit.row >= 0) {
            out.shift_extending = true;
            out.tip_row = hit.row;
            out.tip_col = hit.col;
        }
    }
    // ---- Release frame ----
    if (mouse.left_released) {
        state->drag_painting = false;
        state->drag_extending = false;
        state->drag_row = -1;
        state->drag_col = -1;
    }

    VividIntrospectWidget iw{};
    iw.kind = "step_grid";
    iw.x = bounds.x; iw.y = bounds.y; iw.w = bounds.w; iw.h = bounds.h;
    iw.rows = rows;
    iw.cols = cols;
    iw.active_cols = active_cols;
    iw.anchor_row = state ? state->anchor_row : 0;
    iw.anchor_col = state ? state->anchor_col : 0;
    if (out.cell_clicked)   iw.flags |= VIVID_INTROSPECT_CHANGED;
    if (out.drag_painting || out.shift_extending)
                            iw.flags |= VIVID_INTROSPECT_DRAGGING;
    introspect_emit(ctx, iw);
    return out;
}

// ---------------------------------------------------------------------------
// Drag handle — a circular hotspot that tracks dx/dy from the click point.
// Ideal for point / curve handles in an envelope-style editor (MSEG).
// ---------------------------------------------------------------------------

struct DragHandleState {
    bool  dragging = false;
    float origin_mx = 0.0f;
    float origin_my = 0.0f;
};

struct DragHandleResult {
    bool pressed  = false;  // drag started this frame
    bool dragging = false;  // currently dragging (any frame in the drag)
    bool released = false;  // release happened this frame
    float dx = 0.0f;        // mouse x − origin_mx (delta since drag start)
    float dy = 0.0f;        // mouse y − origin_my
    bool hovered = false;
};

inline DragHandleResult ui_drag_handle(VividEditorContext& ctx,
                                       float cx, float cy, float radius,
                                       DragHandleState* state) {
    DragHandleResult out;
    if (!state) return out;
    const auto& mouse = ctx.mouse;
    const float dx = mouse.x - cx;
    const float dy = mouse.y - cy;
    const float dist_sq = dx * dx + dy * dy;
    const float r2 = radius * radius;
    out.hovered = dist_sq <= r2;

    if (mouse.left_clicked && out.hovered) {
        state->dragging = true;
        state->origin_mx = mouse.x;
        state->origin_my = mouse.y;
        out.pressed = true;
    }
    if (state->dragging) {
        out.dragging = true;
        out.dx = mouse.x - state->origin_mx;
        out.dy = mouse.y - state->origin_my;
    }
    if (state->dragging && mouse.left_released) {
        state->dragging = false;
        out.dragging = false;
        out.released = true;
    }
    VividIntrospectWidget iw{};
    iw.kind = "drag_handle";
    iw.x = cx - radius; iw.y = cy - radius;
    iw.w = radius * 2.0f; iw.h = radius * 2.0f;
    if (out.hovered)  iw.flags |= VIVID_INTROSPECT_HOVERED;
    if (out.pressed)  iw.flags |= VIVID_INTROSPECT_PRESSED;
    if (out.dragging) iw.flags |= VIVID_INTROSPECT_DRAGGING;
    if (out.released) iw.flags |= VIVID_INTROSPECT_CHANGED;
    introspect_emit(ctx, iw);
    return out;
}

// Picker-driven variant. Use when your operator has its own nearest-hit
// logic (e.g. MSEG's pick_point across many potentially-overlapping point
// handles) — `ui_drag_handle_begin` captures the click origin once, and
// `ui_drag_handle_update` reports per-frame dragging + release without
// doing any hit-testing itself.
inline void ui_drag_handle_begin(VividEditorContext& ctx, DragHandleState* state) {
    if (!state) return;
    state->dragging = true;
    state->origin_mx = ctx.mouse.x;
    state->origin_my = ctx.mouse.y;
}

inline DragHandleResult ui_drag_handle_update(VividEditorContext& ctx,
                                              DragHandleState* state) {
    DragHandleResult out;
    if (!state || !state->dragging) return out;
    const auto& mouse = ctx.mouse;
    out.dragging = true;
    out.dx = mouse.x - state->origin_mx;
    out.dy = mouse.y - state->origin_my;
    if (mouse.left_released) {
        state->dragging = false;
        out.dragging = false;
        out.released = true;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Vertical scroll region — clips content to `bounds` and exposes a scroll
// offset that the caller applies when drawing content. Scroll is driven
// by mouse wheel events inside `bounds`; a thin thumb on the right edge is
// also draggable.
//
// Usage:
//   auto content_origin = ui_scroll_region_begin(ctx, bounds, content_h, &st);
//   // draw content starting at content_origin; Y is adjusted for scroll.
//   ui_scroll_region_end(ctx, bounds, content_h, &st);
//
// content_h is the full (unscrolled) height of the content. If the content
// is shorter than `bounds.h`, scroll_y is clamped to 0.
// ---------------------------------------------------------------------------

struct ScrollState {
    float scroll_y = 0.0f;
    bool  dragging_thumb = false;
    float thumb_drag_start_scroll = 0.0f;
    float thumb_drag_start_my = 0.0f;
};

namespace detail {

constexpr float kScrollThumbW = 6.0f;

inline Rect scroll_thumb_rect(Rect bounds, float content_h, float scroll_y) {
    if (content_h <= bounds.h || bounds.h <= 0.0f) return Rect{};
    const float track_h = bounds.h;
    const float thumb_h = std::max(24.0f, track_h * (bounds.h / content_h));
    const float travel  = std::max(0.0f, track_h - thumb_h);
    const float frac    = std::clamp(scroll_y / (content_h - bounds.h), 0.0f, 1.0f);
    const float thumb_y = bounds.y + travel * frac;
    return Rect{
        bounds.x + bounds.w - kScrollThumbW - 1.0f,
        thumb_y,
        kScrollThumbW,
        thumb_h
    };
}

} // namespace detail

// Returns the rect the caller should treat as (0,0) of content space —
// its .x/.y already include the scroll offset.  Pushes a clip.
inline Rect ui_scroll_region_begin(VividEditorContext& ctx, Rect bounds,
                                   float content_h, ScrollState* state) {
    if (!state) return bounds;
    // Wheel events inside the region adjust scroll_y.
    const auto& mouse = ctx.mouse;
    const bool mouse_in = bounds.contains(mouse.x, mouse.y);
    if (mouse_in) {
        for (uint32_t i = 0; i < ctx.event_count; ++i) {
            const auto& e = ctx.events[i];
            if (e.type == VIVID_EDITOR_EVENT_MOUSE_SCROLL) {
                state->scroll_y -= e.scroll_dy * 30.0f;  // 30 px per wheel tick
            }
        }
    }
    // Thumb drag.
    const Rect thumb = detail::scroll_thumb_rect(bounds, content_h, state->scroll_y);
    if (mouse.left_clicked && thumb.w > 0.0f && thumb.contains(mouse.x, mouse.y)) {
        state->dragging_thumb = true;
        state->thumb_drag_start_scroll = state->scroll_y;
        state->thumb_drag_start_my = mouse.y;
    }
    if (state->dragging_thumb) {
        const float track_h = bounds.h;
        const float thumb_h = std::max(24.0f, track_h * (bounds.h / std::max(1.0f, content_h)));
        const float travel  = std::max(1.0f, track_h - thumb_h);
        const float my_delta = mouse.y - state->thumb_drag_start_my;
        const float scroll_span = std::max(0.0f, content_h - bounds.h);
        state->scroll_y = state->thumb_drag_start_scroll
            + my_delta * (scroll_span / travel);
        if (mouse.left_released) state->dragging_thumb = false;
    }
    // Clamp scroll_y to content.
    const float max_scroll = std::max(0.0f, content_h - bounds.h);
    state->scroll_y = std::clamp(state->scroll_y, 0.0f, max_scroll);

    if (ctx.draw.push_clip_rect) {
        ctx.draw.push_clip_rect(ctx.draw.opaque,
            bounds.x, bounds.y, bounds.w, bounds.h);
    }
    return Rect{bounds.x, bounds.y - state->scroll_y, bounds.w, content_h};
}

inline void ui_scroll_region_end(VividEditorContext& ctx, Rect bounds,
                                 float content_h, ScrollState* state) {
    if (ctx.draw.pop_clip_rect) ctx.draw.pop_clip_rect(ctx.draw.opaque);
    if (!state) return;
    // Draw a thin thumb on the right edge when content overflows.
    if (content_h > bounds.h && ctx.draw.draw_rect) {
        const Rect thumb = detail::scroll_thumb_rect(bounds, content_h, state->scroll_y);
        ctx.draw.draw_rect(ctx.draw.opaque,
            thumb.x, thumb.y, thumb.w, thumb.h,
            VividColor{ctx.theme.dim_text.r, ctx.theme.dim_text.g,
                       ctx.theme.dim_text.b, 0.4f});
    }
}

// ---------------------------------------------------------------------------
// Single-line text entry. ASCII-focused for v1 (no multibyte UTF-8 cursor
// movement, no IME). Widget is pure input-plus-buffer-mutation; it never
// calls set_string_param — the caller reads `changed` / `committed` and
// decides whether to commit the buffer to a param. Matches the one-writer
// discipline of every other widget in this toolkit.
//
// Focus model: clicking inside the widget rect focuses; clicking outside
// defocuses. While focused, `ctx.wants_keyboard` is set each frame.
//
// Keys handled (via editor_keys.h constants):
//   Left / Right / Home / End  — cursor nav
//   Shift + nav                — extend selection from anchor
//   Backspace / Delete         — remove char or selection
//   Enter                      — result.committed
//   Escape                     — result.cancelled
//   Cmd/Ctrl + A               — select all
//   Cmd/Ctrl + C / X / V       — clipboard (via ctx.host, guarded)
//
// CHAR events insert at cursor (shifting the tail), replacing any active
// selection. Buffer capacity includes the trailing NUL byte; writes are
// clamped so the buffer never overflows.
//
// Rendering: draw_panel backdrop tinted by focus, current text, selection
// rectangle where applicable, and a blinking caret (500 ms cycle via
// ctx.time) while focused. Placeholder text renders in dim colour when
// the buffer is empty and the field is unfocused.
// ---------------------------------------------------------------------------

struct TextFieldState {
    int    cursor           = 0;    // byte offset into the buffer
    int    selection_anchor = -1;   // -1 = no selection
    bool   focused          = false;
    double last_edit_time   = 0.0;  // drives caret blink + "just-edited" feel
};

struct TextFieldResult {
    bool focused   = false;  // widget has focus this frame
    bool changed   = false;  // buffer mutated this frame
    bool committed = false;  // Enter pressed while focused
    bool cancelled = false;  // Escape pressed while focused
};

namespace detail {

// Remove the active selection from *buffer (inclusive-exclusive: range
// is [min(sel_a, cur), max(sel_a, cur))). Updates *cursor and clears
// *selection_anchor. Returns true if anything was removed.
inline bool text_field_erase_selection(char* buffer, int* cursor,
                                       int* selection_anchor) {
    if (*selection_anchor < 0) return false;
    int a = std::min(*selection_anchor, *cursor);
    int b = std::max(*selection_anchor, *cursor);
    if (a == b) { *selection_anchor = -1; return false; }
    const int len = static_cast<int>(std::strlen(buffer));
    if (b > len) b = len;
    if (a > len) a = len;
    const int tail = len - b;
    for (int i = 0; i < tail; ++i) buffer[a + i] = buffer[b + i];
    buffer[a + tail] = '\0';
    *cursor = a;
    *selection_anchor = -1;
    return true;
}

// Insert one character at *cursor, shifting the tail right. Respects
// buffer_size (must leave room for trailing NUL). Returns true on write.
inline bool text_field_insert_char(char* buffer, std::size_t buffer_size,
                                   int* cursor, char c) {
    const int len = static_cast<int>(std::strlen(buffer));
    if (static_cast<std::size_t>(len + 1) >= buffer_size) return false;
    for (int i = len; i >= *cursor; --i) buffer[i + 1] = buffer[i];
    buffer[*cursor] = c;
    *cursor += 1;
    return true;
}

// Insert a NUL-terminated string at *cursor, clipping to fit. Returns
// the number of chars actually written.
inline int text_field_insert_str(char* buffer, std::size_t buffer_size,
                                 int* cursor, const char* s) {
    if (!s) return 0;
    int written = 0;
    while (*s) {
        if (!text_field_insert_char(buffer, buffer_size, cursor, *s)) break;
        ++s;
        ++written;
    }
    return written;
}

} // namespace detail

inline TextFieldResult ui_text_field(VividEditorContext& ctx, Rect r,
                                     char* buffer, std::size_t buffer_size,
                                     TextFieldState* state,
                                     const char* placeholder = nullptr) {
    TextFieldResult out;
    if (!state || !buffer || buffer_size == 0) return out;

    const auto& mouse = ctx.mouse;

    // --- Focus handling via left-click ---
    if (mouse.left_clicked) {
        if (r.contains(mouse.x, mouse.y)) {
            if (!state->focused) {
                state->focused = true;
                state->selection_anchor = -1;
                state->last_edit_time = ctx.time;
            }
            // Position cursor near the click. Text width helper is
            // approximate; fine-grained click-to-position is a v2 item.
            const float glyph_w = ctx.draw.text_width
                ? std::max(1.0f, ctx.draw.text_width(ctx.draw.opaque, "M", 1.0f))
                : 7.0f;
            const int   len = static_cast<int>(std::strlen(buffer));
            const float pad = 6.0f;
            const int   target = std::clamp(
                static_cast<int>((mouse.x - (r.x + pad)) / glyph_w + 0.5f),
                0, len);
            state->cursor = target;
            state->selection_anchor = -1;
        } else if (state->focused) {
            state->focused = false;
            state->selection_anchor = -1;
        }
    }

    out.focused = state->focused;

    // --- Keyboard / character events ---
    if (state->focused) {
        ctx.wants_keyboard = 1;
        for (uint32_t i = 0; i < ctx.event_count; ++i) {
            const auto& e = ctx.events[i];

            if (e.type == VIVID_EDITOR_EVENT_CHAR) {
                if (e.codepoint < 0x20 || e.codepoint >= 0x7F) continue;  // ASCII only
                detail::text_field_erase_selection(
                    buffer, &state->cursor, &state->selection_anchor);
                if (detail::text_field_insert_char(
                        buffer, buffer_size,
                        &state->cursor, static_cast<char>(e.codepoint))) {
                    out.changed = true;
                    state->last_edit_time = ctx.time;
                }
                continue;
            }

            if (e.type != VIVID_EDITOR_EVENT_KEY) continue;
            if (e.action != ::vivid::editor_keys::kPress &&
                e.action != ::vivid::editor_keys::kRepeat) continue;

            namespace ek = ::vivid::editor_keys;
            const bool shift = (e.modifiers & ek::kModShift) != 0;
            const bool cmd_or_ctrl = ek::is_cmd_or_ctrl(e.modifiers);
            const int  len = static_cast<int>(std::strlen(buffer));

            auto begin_or_extend_selection = [&]() {
                if (shift) {
                    if (state->selection_anchor < 0)
                        state->selection_anchor = state->cursor;
                } else {
                    state->selection_anchor = -1;
                }
            };

            if (e.key == ek::kLeft) {
                begin_or_extend_selection();
                state->cursor = std::max(0, state->cursor - 1);
            } else if (e.key == ek::kRight) {
                begin_or_extend_selection();
                state->cursor = std::min(len, state->cursor + 1);
            } else if (e.key == ek::kHome) {
                begin_or_extend_selection();
                state->cursor = 0;
            } else if (e.key == ek::kEnd) {
                begin_or_extend_selection();
                state->cursor = len;
            } else if (e.key == ek::kBackspace) {
                if (!detail::text_field_erase_selection(
                        buffer, &state->cursor, &state->selection_anchor)) {
                    if (state->cursor > 0) {
                        const int c = state->cursor;
                        for (int k = c - 1; k < len; ++k) buffer[k] = buffer[k + 1];
                        state->cursor = c - 1;
                    }
                }
                out.changed = true;
                state->last_edit_time = ctx.time;
            } else if (e.key == ek::kDelete) {
                if (!detail::text_field_erase_selection(
                        buffer, &state->cursor, &state->selection_anchor)) {
                    if (state->cursor < len) {
                        for (int k = state->cursor; k < len; ++k) buffer[k] = buffer[k + 1];
                    }
                }
                out.changed = true;
                state->last_edit_time = ctx.time;
            } else if (e.key == ek::kEnter) {
                out.committed = true;
            } else if (e.key == ek::kEscape) {
                out.cancelled = true;
                state->focused = false;
                state->selection_anchor = -1;
            } else if (e.key == ek::kA && cmd_or_ctrl) {
                state->selection_anchor = 0;
                state->cursor = len;
            } else if (e.key == ek::kC && cmd_or_ctrl) {
                if (state->selection_anchor >= 0 &&
                    ctx.host.set_clipboard_text && ctx.host.opaque) {
                    const int a = std::min(state->selection_anchor, state->cursor);
                    const int b = std::max(state->selection_anchor, state->cursor);
                    char tmp[512];
                    const int n = std::min(b - a, static_cast<int>(sizeof(tmp) - 1));
                    for (int k = 0; k < n; ++k) tmp[k] = buffer[a + k];
                    tmp[n] = '\0';
                    ctx.host.set_clipboard_text(ctx.host.opaque, tmp);
                }
            } else if (e.key == ek::kX && cmd_or_ctrl) {
                if (state->selection_anchor >= 0 &&
                    ctx.host.set_clipboard_text && ctx.host.opaque) {
                    const int a = std::min(state->selection_anchor, state->cursor);
                    const int b = std::max(state->selection_anchor, state->cursor);
                    char tmp[512];
                    const int n = std::min(b - a, static_cast<int>(sizeof(tmp) - 1));
                    for (int k = 0; k < n; ++k) tmp[k] = buffer[a + k];
                    tmp[n] = '\0';
                    ctx.host.set_clipboard_text(ctx.host.opaque, tmp);
                    detail::text_field_erase_selection(
                        buffer, &state->cursor, &state->selection_anchor);
                    out.changed = true;
                    state->last_edit_time = ctx.time;
                }
            } else if (e.key == ek::kV && cmd_or_ctrl) {
                if (ctx.host.get_clipboard_text && ctx.host.opaque) {
                    const char* s = ctx.host.get_clipboard_text(ctx.host.opaque);
                    if (s && *s) {
                        detail::text_field_erase_selection(
                            buffer, &state->cursor, &state->selection_anchor);
                        if (detail::text_field_insert_str(
                                buffer, buffer_size, &state->cursor, s) > 0) {
                            out.changed = true;
                            state->last_edit_time = ctx.time;
                        }
                    }
                }
            }
        }
    }

    // --- Rendering ---
    const auto& th = ctx.theme;
    const VividColor bg = state->focused
        ? VividColor{th.dark_bg.r * 1.1f, th.dark_bg.g * 1.1f,
                     th.dark_bg.b * 1.1f, 1.0f}
        : VividColor{th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 1.0f};
    const VividColor border = state->focused
        ? VividColor{th.accent.r, th.accent.g, th.accent.b, 0.95f}
        : VividColor{th.separator.r, th.separator.g, th.separator.b, 0.75f};
    vivid::draw_ui::draw_panel(ctx.draw, ctx.draw.opaque,
        r.x, r.y, r.w, r.h, bg, border, 2.0f, 1.0f);

    constexpr float kPad = 6.0f;
    const float text_y = r.y + std::max(0.0f, (r.h - 12.0f) * 0.5f - 1.0f);

    const float glyph_w = ctx.draw.text_width
        ? std::max(1.0f, ctx.draw.text_width(ctx.draw.opaque, "M", 1.0f))
        : 7.0f;

    // Selection highlight (drawn under text).
    if (state->focused && state->selection_anchor >= 0 && ctx.draw.draw_rect) {
        const int a = std::min(state->selection_anchor, state->cursor);
        const int b = std::max(state->selection_anchor, state->cursor);
        const float sx = r.x + kPad + glyph_w * static_cast<float>(a);
        const float sw = glyph_w * static_cast<float>(b - a);
        ctx.draw.draw_rect(ctx.draw.opaque,
            sx, r.y + 2.0f, sw, r.h - 4.0f,
            VividColor{th.accent.r, th.accent.g, th.accent.b, 0.35f});
    }

    // Text (or placeholder).
    const int buffer_len = static_cast<int>(std::strlen(buffer));
    if (buffer_len > 0) {
        if (ctx.draw.draw_text)
            ctx.draw.draw_text(ctx.draw.opaque,
                r.x + kPad, text_y, buffer,
                VividColor{th.bright_text.r, th.bright_text.g,
                           th.bright_text.b, 0.95f}, 1.0f);
    } else if (!state->focused && placeholder && ctx.draw.draw_text) {
        ctx.draw.draw_text(ctx.draw.opaque,
            r.x + kPad, text_y, placeholder,
            VividColor{th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.7f},
            1.0f);
    }

    // Blinking caret (500 ms cycle). Suppress blink briefly after edits so
    // the caret is visible during active typing.
    if (state->focused && ctx.draw.draw_rect) {
        const double since_edit = ctx.time - state->last_edit_time;
        const bool   on = since_edit < 0.15 ||
                          (std::fmod(ctx.time, 1.0) < 0.5);
        if (on) {
            const float cx = r.x + kPad + glyph_w * static_cast<float>(state->cursor);
            ctx.draw.draw_rect(ctx.draw.opaque,
                cx, r.y + 3.0f, 1.0f, r.h - 6.0f,
                VividColor{th.bright_text.r, th.bright_text.g,
                           th.bright_text.b, 0.95f});
        }
    }

    VividIntrospectWidget iw{};
    iw.kind = "text_field";
    iw.x = r.x; iw.y = r.y; iw.w = r.w; iw.h = r.h;
    iw.text = buffer;
    iw.placeholder = placeholder;
    iw.int_value = state ? state->cursor : 0;
    if (state && state->focused) iw.flags |= VIVID_INTROSPECT_FOCUSED;
    if (out.committed || out.changed) iw.flags |= VIVID_INTROSPECT_CHANGED;
    introspect_emit(ctx, iw);
    return out;
}

} // namespace vivid::ui
