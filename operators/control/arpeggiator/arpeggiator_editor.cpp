// Dedicated editor window for Arpeggiator. 4-row × 16-column grid:
//   row 0 — Note Override (Cthulhu-inspired): follow/1-8/mute per step
//   row 1 — Velocity scale (0..1)
//   row 2 — Transpose (±24 semitones)
//   row 3 — Gate length multiplier (0..1)
//
// Keyboard-first, mouse-second. Drag-paints values inside a cell;
// shift-click extends selection rectangle across lanes + steps;
// digit entry sets values directly. Emits set_param commands only —
// never mutates the operator's live params directly.

#include "arpeggiator_core.h"
#include "arpeggiator_editor_shared.h"
#include "arpeggiator_patterns.h"
#include "operator_api/draw_ui_helpers.h"
#include "operator_api/editor_keys.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace arp_ed {

constexpr float kInset      = 8.0f;
constexpr float kTopBarH    = 28.0f;
constexpr float kHeaderW    = 56.0f;   // lane-label gutter on the left
constexpr float kSidePanelW = 220.0f;

// Lane colouring — mild accents per row so the eye can latch onto a lane.
constexpr float kLaneColor[4][3] = {
    {0.82f, 0.55f, 0.30f},  // Note Override — warm orange
    {0.45f, 0.75f, 0.55f},  // Velocity — green
    {0.55f, 0.55f, 0.85f},  // Transpose — blue
    {0.85f, 0.70f, 0.30f},  // Gate — amber
};

} // namespace arp_ed


VividEditorMetadata ArpeggiatorCore::editor_metadata() {
    VividEditorMetadata m{};
    m.default_width  = 1000;
    m.default_height = 420;
    m.min_width      = 720;
    m.min_height     = 320;
    m.title_suffix   = "Arpeggiator Editor";
    return m;
}

void ArpeggiatorCore::draw_editor(VividEditorContext* ctx) {
    if (!ctx) return;
    namespace ed  = ::arp_ed;
    namespace ek  = ::vivid::editor_keys;
    namespace ae  = ::vivid::arpeggiator_editor;
    namespace ui  = ::vivid::ui;           // widgets (Rect, ui_step_grid, GridState)
    namespace sel = ::vivid::editor_ui;    // shared selection helpers

    auto& d = ctx->draw;
    void* o = d.opaque;
    const auto& th = ctx->theme;

    auto get_param = [&](int idx, float fallback) -> float {
        if (idx < 0) return fallback;
        if (static_cast<uint32_t>(idx) >= ctx->param_count) return fallback;
        return ctx->param_values[idx];
    };
    auto set_named = [&](const char* name, float v) {
        if (ctx->commands.set_param)
            ctx->commands.set_param(ctx->commands.opaque, name, v);
    };
    auto set_cell = [&](ae::Lane lane, int step, float v) {
        set_named(ae::param_name_for(lane, step).c_str(), v);
    };

    // ---- Live state reads ----
    const int num_steps = std::clamp(
        static_cast<int>(std::lround(get_param(ae::kModStepsIndex, 8.0f))),
        1, ae::kMaxSteps);
    const int current_step = (ctx->output_count > ae::kStepOutputIndex)
        ? static_cast<int>(ctx->output_values[ae::kStepOutputIndex]) : -1;
    const int current_mod = (current_step >= 0) ? (current_step % num_steps) : -1;
    const int mode_val = std::clamp(
        static_cast<int>(std::lround(get_param(ae::kModeIndex, 0.0f))), 0, 9);

    // ---- Clamp persistent state ----
    {
        ae::SelectionLike sel{};
        sel.row_lo = editor_selection_.row_lo;
        sel.row_hi = editor_selection_.row_hi;
        sel.col_lo = editor_selection_.col_lo;
        sel.col_hi = editor_selection_.col_hi;
        ae::clamp_editor_state(num_steps,
                               &editor_cursor_row_, &editor_cursor_step_,
                               &grid_state_.anchor_row, &grid_state_.anchor_col,
                               &sel);
        editor_selection_.row_lo = sel.row_lo;
        editor_selection_.row_hi = sel.row_hi;
        editor_selection_.col_lo = sel.col_lo;
        editor_selection_.col_hi = sel.col_hi;
    }
    auto rebuild_selection = [&]() {
        editor_selection_ = sel::selection_from_anchor_tip(
            grid_state_.anchor_row, grid_state_.anchor_col,
            editor_cursor_row_,     editor_cursor_step_);
    };
    auto collapse_selection_to_cursor = [&]() {
        grid_state_.anchor_row = editor_cursor_row_;
        grid_state_.anchor_col = editor_cursor_step_;
        rebuild_selection();
    };
    rebuild_selection();

    auto for_each_selected = [&](auto&& fn) {
        for (int r = editor_selection_.row_lo; r <= editor_selection_.row_hi; ++r) {
            for (int s = editor_selection_.col_lo; s <= editor_selection_.col_hi; ++s) {
                fn(static_cast<ae::Lane>(r), s);
            }
        }
    };

    // ---- Selection-wide editing ops ----
    auto cycle_note_override_cursor = [&]() {
        const int idx = ae::param_index_for(
            ae::Lane::NoteOverride, editor_cursor_step_);
        const int cur = ae::clamp_note_override(
            static_cast<int>(std::lround(get_param(idx, 0.0f))));
        const int next = (cur + 1) % 10;  // 0..9 cycle
        for_each_selected([&](ae::Lane lane, int step) {
            if (lane == ae::Lane::NoteOverride)
                set_cell(lane, step, static_cast<float>(next));
        });
    };
    auto clear_selection_to_defaults = [&]() {
        for_each_selected([&](ae::Lane lane, int step) {
            float v = 0.0f;
            switch (lane) {
                case ae::Lane::NoteOverride: v = 0.0f; break;  // follow
                case ae::Lane::Velocity:     v = 1.0f; break;
                case ae::Lane::Transpose:    v = 0.0f; break;
                case ae::Lane::Gate:         v = 1.0f; break;
            }
            set_cell(lane, step, v);
        });
    };
    auto set_selection_from_digit = [&](int digit) {
        for_each_selected([&](ae::Lane lane, int step) {
            float v = 0.0f;
            switch (lane) {
                case ae::Lane::NoteOverride:
                    v = static_cast<float>(std::clamp(digit, 0, 9));
                    break;
                case ae::Lane::Velocity:
                case ae::Lane::Gate:
                    v = std::clamp(static_cast<float>(digit) / 9.0f, 0.0f, 1.0f);
                    break;
                case ae::Lane::Transpose:
                    // Digits on transpose nudge by semitones in whole-octave
                    // chunks: 0 = 0, 1..4 = +N octaves, 5..9 = -(N-4) octaves.
                    v = (digit == 0) ? 0.0f
                      : (digit <= 4) ? static_cast<float>(digit * 12)
                                     : static_cast<float>(-(digit - 4) * 12);
                    break;
            }
            set_cell(lane, step, v);
        });
    };

    // ---- Layout ----
    const float surf_w = ctx->surface_width;
    const float surf_h = ctx->surface_height;
    const float top_y  = ed::kInset;
    const float top_h  = ed::kTopBarH;

    const float grid_x = ed::kInset;
    const float grid_y = top_y + top_h + ed::kInset;
    const float grid_w = std::max(0.0f,
        surf_w - 3.0f * ed::kInset - ed::kSidePanelW);
    const float grid_h = std::max(0.0f, surf_h - grid_y - ed::kInset);

    // Grid body sits past the lane-label gutter on the left.
    const float cells_x = grid_x + ed::kHeaderW;
    const float cells_y = grid_y;
    const float cells_w = std::max(0.0f, grid_w - ed::kHeaderW);
    const float cell_w  = cells_w / static_cast<float>(ae::kMaxSteps);
    const float cell_h  = grid_h / static_cast<float>(ae::kRowCount);

    const ui::Rect cells_bounds{cells_x, cells_y, cells_w, grid_h};

    const float side_x = grid_x + grid_w + ed::kInset;
    const float side_y = grid_y;
    const float side_w = ed::kSidePanelW;
    const float side_h = grid_h;

    // ---- Keyboard ----
    ctx->wants_keyboard = 1;
    for (uint32_t ei = 0; ei < ctx->event_count; ++ei) {
        const auto& e = ctx->events[ei];
        if (e.type != VIVID_EDITOR_EVENT_KEY) continue;
        if (e.action != ek::kPress && e.action != ek::kRepeat) continue;

        const bool shift = (e.modifiers & ek::kModShift) != 0;
        const bool cmd_or_ctrl = ek::is_cmd_or_ctrl(e.modifiers);

        // Arrow nav: move cursor; Shift extends selection.
        int dx = 0, dy = 0;
        if      (e.key == ek::kUp)    dy = -1;
        else if (e.key == ek::kDown)  dy = +1;
        else if (e.key == ek::kLeft)  dx = -1;
        else if (e.key == ek::kRight) dx = +1;
        if (dx != 0 || dy != 0) {
            if (shift && editor_selection_.row_lo == editor_selection_.row_hi &&
                          editor_selection_.col_lo == editor_selection_.col_hi) {
                // Seed anchor from current cursor before the move so the
                // first shift+arrow includes the starting cell.
                grid_state_.anchor_row = editor_cursor_row_;
                grid_state_.anchor_col = editor_cursor_step_;
            }
            sel::cursor_move(dx, dy,
                            ae::kRowCount - 1, num_steps - 1,
                            &editor_cursor_row_, &editor_cursor_step_);
            if (shift) rebuild_selection();
            else       collapse_selection_to_cursor();
            continue;
        }
        if (e.key == ek::kTab) {
            // Jump between rows with the same step column.
            editor_cursor_row_ = (editor_cursor_row_ + (shift ? -1 : +1)
                                  + ae::kRowCount) % ae::kRowCount;
            collapse_selection_to_cursor();
            continue;
        }
        if (e.key == ek::kEnter) {
            // Row-specific toggle: Note Override cycles, others set max.
            if (editor_cursor_row_ == static_cast<int>(ae::Lane::NoteOverride)) {
                cycle_note_override_cursor();
            } else {
                for_each_selected([&](ae::Lane lane, int step) {
                    float v = 1.0f;
                    if (lane == ae::Lane::Transpose) v = 12.0f;  // +1 octave
                    set_cell(lane, step, v);
                });
            }
            continue;
        }
        if (e.key == ek::kSpace) {
            clear_selection_to_defaults();
            continue;
        }
        if (e.key == ek::kEscape) {
            collapse_selection_to_cursor();
            continue;
        }
        if (!cmd_or_ctrl && ek::is_digit_key(e.key)) {
            set_selection_from_digit(ek::digit_value(e.key));
            continue;
        }
        if (cmd_or_ctrl && e.key == ek::kC) {
            const int rows = editor_selection_.row_hi - editor_selection_.row_lo + 1;
            const int cols = editor_selection_.col_hi - editor_selection_.col_lo + 1;
            if (rows > 0 && cols > 0 && rows <= 4 && cols <= 16) {
                editor_clipboard_.rows = rows;
                editor_clipboard_.cols = cols;
                for (int r = 0; r < rows; ++r) {
                    for (int c = 0; c < cols; ++c) {
                        const int idx = ae::param_index_for(
                            static_cast<ae::Lane>(editor_selection_.row_lo + r),
                            editor_selection_.col_lo + c);
                        editor_clipboard_.values[r * cols + c] =
                            get_param(idx, 0.0f);
                    }
                }
                editor_clipboard_.has_content = true;
            }
            continue;
        }
        if (cmd_or_ctrl && e.key == ek::kV) {
            if (editor_clipboard_.has_content) {
                const int origin_row = editor_selection_.row_lo;
                const int origin_col = editor_selection_.col_lo;
                for (int r = 0; r < editor_clipboard_.rows; ++r) {
                    const int row = origin_row + r;
                    if (row < 0 || row >= ae::kRowCount) continue;
                    for (int c = 0; c < editor_clipboard_.cols; ++c) {
                        const int col = origin_col + c;
                        if (col < 0 || col >= num_steps) continue;
                        set_cell(static_cast<ae::Lane>(row), col,
                                 editor_clipboard_.values[r * editor_clipboard_.cols + c]);
                    }
                }
            }
            continue;
        }
    }

    // ---- Mouse: ui_step_grid handles click / drag-paint / shift-extend ----
    auto grid_res = ui::ui_step_grid(*ctx, cells_bounds,
                                     ae::kRowCount, ae::kMaxSteps,
                                     num_steps, &grid_state_);

    if (grid_res.shift_extending) {
        editor_cursor_row_  = grid_res.tip_row;
        editor_cursor_step_ = grid_res.tip_col;
        rebuild_selection();
    } else if (grid_res.cell_clicked) {
        editor_cursor_row_  = grid_res.clicked_row;
        editor_cursor_step_ = grid_res.clicked_col;
        if (!grid_res.clicked_with_shift) collapse_selection_to_cursor();
    }
    if (grid_res.drag_painting &&
        grid_res.drag_row >= 0 && grid_res.drag_col >= 0) {
        const ae::Lane lane = static_cast<ae::Lane>(grid_res.drag_row);
        const int step = grid_res.drag_col;
        // y_in_cell: 0 = top, 1 = bottom. Convert per lane semantics.
        const float y = grid_res.drag_mouse_y_in_cell;
        switch (lane) {
            case ae::Lane::NoteOverride: {
                // Click only; cycle on first-frame click to avoid
                // spinning through states while the mouse is held.
                if (ctx->mouse.left_clicked)
                    cycle_note_override_cursor();
                break;
            }
            case ae::Lane::Velocity: {
                const float v = std::clamp(1.0f - y, 0.0f, 1.0f);
                set_cell(lane, step, v);
                break;
            }
            case ae::Lane::Transpose: {
                // Centered bipolar: y = 0.5 → 0, y = 0 → +24, y = 1 → -24.
                const float norm = std::clamp((0.5f - y) * 2.0f, -1.0f, 1.0f);
                const int semi = static_cast<int>(std::lround(norm * 24.0f));
                set_cell(lane, step, static_cast<float>(semi));
                break;
            }
            case ae::Lane::Gate: {
                const float v = std::clamp(1.0f - y, 0.0f, 1.0f);
                set_cell(lane, step, v);
                break;
            }
        }
    }

    // ---- Drawing ----
    vivid::draw_ui::draw_panel(d, o, grid_x, grid_y, grid_w, grid_h,
        {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 0.92f});

    // Top bar text.
    if (d.draw_text) {
        static const char* kModes[] = {
            "Up","Down","UpDown","DownUp","Random",
            "Order","Converge","Diverge","RandNoRep","OrderDown"};
        char buf[128];
        std::snprintf(buf, sizeof(buf),
            "Mode: %s   Steps: %d",
            kModes[std::clamp(mode_val, 0, 9)], num_steps);
        d.draw_text(o, grid_x, top_y + 6.0f, buf,
            {th.bright_text.r, th.bright_text.g, th.bright_text.b, 0.95f},
            1.0f);

        const char* hints =
            "— / 1-8 / M per step  ·  drag = value  ·  arrows/Tab  ·  "
            "0-9 set  ·  Enter  ·  Space  ·  Cmd+C/V";
        const float hints_scale = 0.7f;
        const float hints_w = d.text_width
            ? d.text_width(o, hints, hints_scale) : 540.0f;
        d.draw_text(o, grid_x + grid_w - hints_w, top_y + 8.0f, hints,
            {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.7f}, hints_scale);
    }

    // Lane labels.
    static const char* kLaneNames[4] = {"note", "vel", "tr", "gate"};
    for (int r = 0; r < ae::kRowCount; ++r) {
        const float ly = cells_y + r * cell_h;
        if (d.draw_text) {
            d.draw_text(o, grid_x + 4.0f, ly + cell_h * 0.5f - 6.0f,
                kLaneNames[r],
                {ed::kLaneColor[r][0], ed::kLaneColor[r][1],
                 ed::kLaneColor[r][2], 0.9f}, 1.0f);
        }
    }

    // Row separators.
    if (d.draw_rect) {
        for (int r = 1; r < ae::kRowCount; ++r) {
            const float y = cells_y + r * cell_h - 0.5f;
            d.draw_rect(o, grid_x, y, grid_w, 1.0f,
                {th.separator.r, th.separator.g, th.separator.b, 0.55f});
        }
    }

    // Beat separators every 4 steps.
    if (d.draw_rect) {
        for (int s = 4; s < ae::kMaxSteps; s += 4) {
            const float x = cells_x + s * cell_w - 0.5f;
            d.draw_rect(o, x, grid_y, 1.0f, grid_h,
                {th.separator.r, th.separator.g, th.separator.b, 0.4f});
        }
    }

    // Current step column highlight.
    if (current_mod >= 0 && current_mod < num_steps && cell_w > 0.0f) {
        vivid::draw_ui::draw_selection_highlight(d, o,
            cells_x + current_mod * cell_w, grid_y, cell_w, grid_h,
            {th.accent.r, th.accent.g, th.accent.b, 1.0f}, 0.15f);
    }

    // Cells.
    for (int step = 0; step < ae::kMaxSteps; ++step) {
        const float cx = cells_x + step * cell_w;
        const bool beyond = (step >= num_steps);
        const float pad = 2.0f;
        if (cell_w <= pad * 2.0f || cell_h <= pad * 2.0f) continue;

        // Row 0: Note Override — letter / digit glyph
        {
            const float ix = cx + pad;
            const float iy = cells_y + pad;
            const float iw = std::max(0.0f, cell_w - 2.0f * pad);
            const float ih = std::max(0.0f, cell_h - 2.0f * pad);
            vivid::draw_ui::draw_panel(d, o, ix, iy, iw, ih,
                {0.12f, 0.13f, 0.15f, beyond ? 0.35f : 0.9f},
                {0, 0, 0, 0}, 2.0f);
            if (!beyond) {
                const int v = ae::clamp_note_override(static_cast<int>(std::lround(
                    get_param(ae::param_index_for(ae::Lane::NoteOverride, step),
                              0.0f))));
                const char* label = ae::note_override_label(v);
                const VividColor col = (v == 0)
                    ? VividColor{th.dim_text.r, th.dim_text.g,
                                 th.dim_text.b, 0.7f}
                  : (v == 9)
                    ? VividColor{0.9f, 0.35f, 0.35f, 0.95f}  // mute = red
                    : VividColor{ed::kLaneColor[0][0],
                                 ed::kLaneColor[0][1],
                                 ed::kLaneColor[0][2], 0.95f};
                if (d.draw_text)
                    d.draw_text(o, ix + iw * 0.5f - 4.0f, iy + ih * 0.5f - 6.0f,
                                label, col, 1.0f);
            }
        }

        // Row 1: Velocity fader (fill bottom-up)
        {
            const float ix = cx + pad;
            const float iy = cells_y + cell_h + pad;
            const float iw = std::max(0.0f, cell_w - 2.0f * pad);
            const float ih = std::max(0.0f, cell_h - 2.0f * pad);
            vivid::draw_ui::draw_panel(d, o, ix, iy, iw, ih,
                {0.12f, 0.13f, 0.15f, beyond ? 0.35f : 0.9f});
            if (!beyond) {
                const float v = std::clamp(get_param(
                    ae::param_index_for(ae::Lane::Velocity, step), 1.0f),
                    0.0f, 1.0f);
                const float bar_h = ih * v;
                if (bar_h > 0.0f && d.draw_rect) {
                    d.draw_rect(o, ix + 1.0f, iy + ih - bar_h,
                        std::max(0.0f, iw - 2.0f), bar_h,
                        {ed::kLaneColor[1][0], ed::kLaneColor[1][1],
                         ed::kLaneColor[1][2], 0.75f});
                }
            }
        }

        // Row 2: Transpose (centered bipolar bar)
        {
            const float ix = cx + pad;
            const float iy = cells_y + 2 * cell_h + pad;
            const float iw = std::max(0.0f, cell_w - 2.0f * pad);
            const float ih = std::max(0.0f, cell_h - 2.0f * pad);
            vivid::draw_ui::draw_panel(d, o, ix, iy, iw, ih,
                {0.12f, 0.13f, 0.15f, beyond ? 0.35f : 0.9f});
            if (!beyond) {
                const float tr = std::clamp(get_param(
                    ae::param_index_for(ae::Lane::Transpose, step), 0.0f),
                    -24.0f, 24.0f);
                const float norm = tr / 24.0f;
                const float mid = iy + ih * 0.5f;
                const float bar = std::abs(norm) * (ih * 0.5f);
                const float by  = (norm >= 0.0f) ? (mid - bar) : mid;
                if (bar > 0.0f && d.draw_rect) {
                    d.draw_rect(o, ix + 1.0f, by,
                        std::max(0.0f, iw - 2.0f), bar,
                        {ed::kLaneColor[2][0], ed::kLaneColor[2][1],
                         ed::kLaneColor[2][2], 0.75f});
                }
                // Midline tick for reference.
                if (d.draw_rect) {
                    d.draw_rect(o, ix + 1.0f, mid - 0.5f,
                        std::max(0.0f, iw - 2.0f), 1.0f,
                        {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.3f});
                }
            }
        }

        // Row 3: Gate (fill bottom-up)
        {
            const float ix = cx + pad;
            const float iy = cells_y + 3 * cell_h + pad;
            const float iw = std::max(0.0f, cell_w - 2.0f * pad);
            const float ih = std::max(0.0f, cell_h - 2.0f * pad);
            vivid::draw_ui::draw_panel(d, o, ix, iy, iw, ih,
                {0.12f, 0.13f, 0.15f, beyond ? 0.35f : 0.9f});
            if (!beyond) {
                const float g = std::clamp(get_param(
                    ae::param_index_for(ae::Lane::Gate, step), 1.0f),
                    0.0f, 1.0f);
                const float bar_h = ih * g;
                if (bar_h > 0.0f && d.draw_rect) {
                    d.draw_rect(o, ix + 1.0f, iy + ih - bar_h,
                        std::max(0.0f, iw - 2.0f), bar_h,
                        {ed::kLaneColor[3][0], ed::kLaneColor[3][1],
                         ed::kLaneColor[3][2], 0.75f});
                }
            }
        }
    }

    // Selection + cursor outlines.
    if (cell_w > 0.0f && cell_h > 0.0f) {
        const auto& sel = editor_selection_;
        const float sx = cells_x + sel.col_lo * cell_w;
        const float sy = cells_y + sel.row_lo * cell_h;
        const float sw = (sel.col_hi - sel.col_lo + 1) * cell_w;
        const float sh = (sel.row_hi - sel.row_lo + 1) * cell_h;
        vivid::draw_ui::draw_panel(d, o, sx, sy, sw, sh,
            {0, 0, 0, 0},
            {th.accent.r, th.accent.g, th.accent.b, 0.9f}, 0.0f, 1.0f);

        const float ccx = cells_x + editor_cursor_step_ * cell_w;
        const float ccy = cells_y + editor_cursor_row_  * cell_h;
        vivid::draw_ui::draw_panel(d, o, ccx, ccy, cell_w, cell_h,
            {0, 0, 0, 0},
            {th.bright_text.r, th.bright_text.g, th.bright_text.b, 1.0f},
            0.0f, 1.5f);
    }

    // ---- Side panel ----
    vivid::draw_ui::draw_panel(d, o, side_x, side_y, side_w, side_h,
        {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 0.85f},
        {th.separator.r, th.separator.g, th.separator.b, 0.8f}, 4.0f, 1.0f);

    if (d.draw_text) {
        constexpr float kSpPad = 10.0f;
        static const char* kLaneLabels[4] = {
            "Note Override", "Velocity", "Transpose", "Gate"};
        char line[128];

        std::snprintf(line, sizeof(line), "Row: %s",
            kLaneLabels[std::clamp(editor_cursor_row_, 0, 3)]);
        d.draw_text(o, side_x + kSpPad, side_y + kSpPad, line,
            {th.bright_text.r, th.bright_text.g, th.bright_text.b, 0.95f}, 1.0f);

        std::snprintf(line, sizeof(line), "Step: %d", editor_cursor_step_ + 1);
        d.draw_text(o, side_x + kSpPad, side_y + kSpPad + 22.0f, line,
            {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.9f}, 1.0f);

        // Cursor cell value readout.
        const int idx = ae::param_index_for(
            static_cast<ae::Lane>(editor_cursor_row_), editor_cursor_step_);
        const float v = get_param(idx, 0.0f);
        switch (static_cast<ae::Lane>(editor_cursor_row_)) {
            case ae::Lane::NoteOverride: {
                const int iv = ae::clamp_note_override(
                    static_cast<int>(std::lround(v)));
                std::snprintf(line, sizeof(line), "value: %s%s",
                    ae::note_override_label(iv),
                    iv == 0 ? "  (follow mode)" :
                    iv == 9 ? "  (mute)" : "");
                break;
            }
            case ae::Lane::Velocity:
                std::snprintf(line, sizeof(line), "value: %.2f", v);
                break;
            case ae::Lane::Transpose:
                std::snprintf(line, sizeof(line), "value: %+d st",
                    static_cast<int>(std::lround(v)));
                break;
            case ae::Lane::Gate:
                std::snprintf(line, sizeof(line),
                    "value: %.2f × gate", std::clamp(v, 0.0f, 1.0f));
                break;
        }
        d.draw_text(o, side_x + kSpPad, side_y + kSpPad + 44.0f, line,
            {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.9f}, 1.0f);

        // Mode-diagram ribbon — shows which pool index the global mode
        // picks for each step. Reads from arpeggiator_patterns shared
        // code so the preview can't diverge from compute().
        const float diag_y = side_y + kSpPad + 88.0f;
        d.draw_text(o, side_x + kSpPad, diag_y, "mode ribbon:",
            {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.75f}, 0.85f);

        const float ribbon_y = diag_y + 16.0f;
        const float ribbon_w = side_w - 2.0f * kSpPad;
        const float step_w   = ribbon_w / static_cast<float>(num_steps);
        // Display assuming a 4-note pool (typical triad + root).
        constexpr int kAssumedPoolCount = 4;
        for (int s = 0; s < num_steps; ++s) {
            const int idx_pool = (mode_val == 4 || mode_val == 8)
                ? 0  // Random modes don't preview usefully — show flat
                : vivid_sequencers::arp_pattern_index(mode_val, s, kAssumedPoolCount);
            const float bar_h = 8.0f +
                static_cast<float>(idx_pool) /
                static_cast<float>(kAssumedPoolCount - 1) * 22.0f;
            const float bx = side_x + kSpPad + s * step_w;
            const float by = ribbon_y + (32.0f - bar_h);
            if (d.draw_rect) {
                const float alpha = (s == current_mod) ? 0.95f : 0.5f;
                d.draw_rect(o, bx + 1.0f, by, std::max(1.0f, step_w - 2.0f),
                    bar_h,
                    {th.accent.r, th.accent.g, th.accent.b, alpha});
            }
        }
    }
}

