// Dedicated editor window for Sequencer. Two-row grid (row 0 = value
// fader, row 1 = gate toggle) × 32 steps. Cursor + rectangular selection
// span both rows so the user can copy/paste arbitrary (row, step)
// rectangles with Cmd+C / Cmd+V. Digits 0–9 set the value across the
// selection (row 0 as digit/9, row 1 as on/off). Enter toggles the cell
// at cursor; Space clears (value → 0, gate → 0).
//
// Live playhead reads output_values[1] (current step index) and tints
// the active column. Bipolar polarity centres the value fader on the
// cell's midline.
//
// Phase-4 platform conventions: widgets from editor_ui.h own hit-test
// and drag continuation; operator only emits set_param / toggles state.

#include "sequencer_core.h"
#include "sequencer_editor_shared.h"
#include "operator_api/draw_ui_helpers.h"
#include "operator_api/editor_keys.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace seq_ed {

constexpr float kTopBarH    = 26.0f;
constexpr float kInset      = 8.0f;
constexpr float kSidePanelW = 220.0f;

} // namespace seq_ed

VividEditorMetadata SequencerCore::editor_metadata() {
    VividEditorMetadata m{};
    m.default_width  = 900;
    m.default_height = 420;
    m.min_width      = 600;
    m.min_height     = 300;
    m.title_suffix   = "Sequencer Editor";
    return m;
}

void SequencerCore::draw_editor(VividEditorContext* ctx) {
    if (!ctx) return;
    namespace ed  = ::seq_ed;
    namespace ek  = ::vivid::editor_keys;
    namespace se  = ::vivid::sequencer_editor;

    auto& d = ctx->draw;
    void* o = d.opaque;
    const auto& th = ctx->theme;

    // ---- Live-param + live-output accessors ----
    auto get_param = [&](int idx, float fallback) -> float {
        if (idx < 0) return fallback;
        if (static_cast<uint32_t>(idx) >= ctx->param_count) return fallback;
        return ctx->param_values[idx];
    };
    auto set_named = [&](const char* name, float v) {
        if (ctx->commands.set_param)
            ctx->commands.set_param(ctx->commands.opaque, name, v);
    };
    auto set_cell = [&](se::RowKind row, int step, float v) {
        set_named(se::param_name_for(row, step).c_str(), v);
    };

    // Current `steps` param (may have shrunk since selection was stored).
    const int num_steps_raw = static_cast<int>(
        std::lround(get_param(se::kStepsParamIndex, 8.0f)));
    const int num_steps = std::clamp(num_steps_raw, 1, se::kMaxSteps);

    // Live playhead — output_values[1] is the current step index.
    const int current_step = (ctx->output_count > se::kStepOutputIndex)
        ? static_cast<int>(ctx->output_values[se::kStepOutputIndex])
        : -1;

    // Bipolar polarity re-centres the value fader on the cell's midline.
    const bool bipolar =
        std::lround(get_param(se::kPolarityParamIndex, 0.0f)) == 0;

    // ---- Clamp persistent state to current num_steps ----
    const int max_row = se::kRowCount - 1;
    const int max_col = num_steps - 1;
    se::clamp_editor_state(max_row, max_col,
                           &editor_cursor_row_, &editor_cursor_step_,
                           &grid_state_.anchor_row, &grid_state_.anchor_col,
                           &editor_selection_);

    auto rebuild_selection = [&]() {
        editor_selection_ = se::selection_from_anchor_tip(
            grid_state_.anchor_row, grid_state_.anchor_col,
            editor_cursor_row_, editor_cursor_step_);
    };
    auto collapse_selection_to_cursor = [&]() {
        grid_state_.anchor_row = editor_cursor_row_;
        grid_state_.anchor_col = editor_cursor_step_;
        rebuild_selection();
    };
    // First frame: ensure cached rect matches anchor+cursor.
    rebuild_selection();

    auto for_each_selected = [&](auto&& fn) {
        for (int r = editor_selection_.row_lo; r <= editor_selection_.row_hi; ++r) {
            for (int s = editor_selection_.col_lo; s <= editor_selection_.col_hi; ++s) {
                fn(r, s);
            }
        }
    };

    // ---- Selection-wide editing ops ----
    auto toggle_cursor_cell = [&]() {
        const se::RowKind row = (editor_cursor_row_ == 0)
            ? se::RowKind::Value : se::RowKind::Gate;
        const int idx = se::param_index_for(row, editor_cursor_step_);
        const float cur = get_param(idx, 0.0f);
        const float target =
            (row == se::RowKind::Value)
                ? (cur > 0.5f ? 0.0f : 1.0f)
                : (cur > 0.5f ? 0.0f : 1.0f);
        for_each_selected([&](int r, int s) {
            const se::RowKind rk = (r == 0) ? se::RowKind::Value
                                            : se::RowKind::Gate;
            set_cell(rk, s, target);
        });
    };
    auto clear_selection = [&]() {
        for_each_selected([&](int r, int s) {
            const se::RowKind rk = (r == 0) ? se::RowKind::Value
                                            : se::RowKind::Gate;
            set_cell(rk, s, 0.0f);
        });
    };
    auto set_selection_from_digit = [&](int digit) {
        // Row 0 (value): digit / 9 ∈ [0, 1]. Row 1 (gate): 0 → off, else on.
        const float value_for_val  = std::clamp(
            static_cast<float>(digit) / 9.0f, 0.0f, 1.0f);
        const float value_for_gate = (digit > 0) ? 1.0f : 0.0f;
        for_each_selected([&](int r, int s) {
            if (r == 0) set_cell(se::RowKind::Value, s, value_for_val);
            else        set_cell(se::RowKind::Gate,  s, value_for_gate);
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

    const float side_x = grid_x + grid_w + ed::kInset;
    const float side_y = grid_y;
    const float side_w = ed::kSidePanelW;
    const float side_h = grid_h;

    const vivid::ui::Rect grid_bounds{grid_x, grid_y, grid_w, grid_h};
    const float cell_w = (grid_w > 0.0f)
        ? grid_w / static_cast<float>(se::kMaxSteps) : 0.0f;
    const float cell_h = (grid_h > 0.0f)
        ? grid_h / static_cast<float>(se::kRowCount) : 0.0f;

    // ---- Keyboard handling ----
    ctx->wants_keyboard = 1;
    for (uint32_t ei = 0; ei < ctx->event_count; ++ei) {
        const auto& e = ctx->events[ei];
        if (e.type != VIVID_EDITOR_EVENT_KEY) continue;
        if (e.action != ek::kPress && e.action != ek::kRepeat) continue;

        const bool shift = (e.modifiers & ek::kModShift) != 0;
        const bool cmd_or_ctrl = ek::is_cmd_or_ctrl(e.modifiers);

        // Arrows: cursor nav; shift extends selection.
        int dx = 0, dy = 0;
        if      (e.key == ek::kUp)    dy = -1;
        else if (e.key == ek::kDown)  dy = +1;
        else if (e.key == ek::kLeft)  dx = -1;
        else if (e.key == ek::kRight) dx = +1;
        if (dx != 0 || dy != 0) {
            se::cursor_move(dx, dy, max_row, max_col,
                            &editor_cursor_row_, &editor_cursor_step_);
            if (shift) rebuild_selection();
            else       collapse_selection_to_cursor();
            continue;
        }

        // Tab: flip between value and gate row, keep step.
        if (e.key == ek::kTab) {
            editor_cursor_row_ = 1 - editor_cursor_row_;
            collapse_selection_to_cursor();
            continue;
        }
        // Enter: toggle cursor cell (across selection).
        if (e.key == ek::kEnter) { toggle_cursor_cell(); continue; }
        // Space: clear selection.
        if (e.key == ek::kSpace) { clear_selection(); continue; }
        // Escape: collapse selection to cursor cell.
        if (e.key == ek::kEscape) {
            collapse_selection_to_cursor();
            continue;
        }
        // Digits: set value across selection (row 0 = digit/9, row 1 = binary).
        if (!cmd_or_ctrl && ek::is_digit_key(e.key)) {
            set_selection_from_digit(ek::digit_value(e.key));
            continue;
        }
        // Cmd+C / Cmd+V: copy/paste rectangle.
        if (e.key == ek::kC && cmd_or_ctrl) {
            se::copy_selection(ctx->param_values, ctx->param_count,
                               editor_selection_, &selection_clipboard_);
            continue;
        }
        if (e.key == ek::kV && cmd_or_ctrl) {
            se::paste_selection(ctx->commands, selection_clipboard_,
                                editor_selection_.row_lo,
                                editor_selection_.col_lo);
            continue;
        }
    }

    // ---- Mouse: click / drag-paint / shift-extend via ui_step_grid ----
    auto grid_res = vivid::ui::ui_step_grid(*ctx, grid_bounds,
                                            se::kRowCount, se::kMaxSteps,
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
        if (grid_res.drag_row == 0) {
            // Value row: cursor Y in cell → value (top = max, bottom = min).
            const float v = std::clamp(
                1.0f - grid_res.drag_mouse_y_in_cell, 0.0f, 1.0f);
            set_cell(se::RowKind::Value, grid_res.drag_col, v);
        } else {
            // Gate row: first drag frame decides the polarity, subsequent
            // frames are no-ops (ui_step_grid keeps drag_row/col pinned
            // to the original click cell, so this naturally fires once
            // when the click happens).
            if (ctx->mouse.left_clicked) {
                const int idx = se::param_index_for(
                    se::RowKind::Gate, grid_res.drag_col);
                const float cur = get_param(idx, 0.0f);
                set_cell(se::RowKind::Gate, grid_res.drag_col,
                         cur > 0.5f ? 0.0f : 1.0f);
            }
        }
    }

    // ---- Drawing ----
    vivid::draw_ui::draw_panel(d, o, grid_x, grid_y, grid_w, grid_h,
        {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 0.9f});

    // Top-bar text.
    if (d.draw_text) {
        char steps_buf[48];
        std::snprintf(steps_buf, sizeof(steps_buf), "Steps: %d", num_steps);
        d.draw_text(o, grid_x, top_y + 4.0f, steps_buf,
                    {th.bright_text.r, th.bright_text.g,
                     th.bright_text.b, 0.9f}, 1.0f);
        const char* polarity_label = bipolar ? "bipolar" : "unipolar";
        d.draw_text(o, grid_x + 90.0f, top_y + 4.0f, polarity_label,
                    {th.dim_text.r, th.dim_text.g,
                     th.dim_text.b, 0.9f}, 1.0f);
        const char* hints =
            "Click+drag = value  ·  Click = toggle gate  ·  "
            "0-9 = set  ·  Enter/Space  ·  Tab  ·  Cmd+C/V";
        const float hints_scale = 0.75f;
        const float hints_w = d.text_width
            ? d.text_width(o, hints, hints_scale) : 400.0f;
        d.draw_text(o, grid_x + grid_w - hints_w, top_y + 6.0f, hints,
                    {th.dim_text.r, th.dim_text.g,
                     th.dim_text.b, 0.75f}, hints_scale);
    }

    // Beat-group separators every 4 steps.
    for (int b = 1; b < se::kMaxSteps / 4; ++b) {
        const float sx = grid_x + b * 4 * cell_w;
        if (d.draw_rect) {
            d.draw_rect(o, sx - 0.5f, grid_y, 1.0f, grid_h,
                        {th.separator.r, th.separator.g,
                         th.separator.b, 0.55f});
        }
    }

    // Current-step column highlight (across both rows).
    if (current_step >= 0 && current_step < num_steps && cell_w > 0.0f) {
        vivid::draw_ui::draw_selection_highlight(
            d, o,
            grid_x + current_step * cell_w, grid_y,
            cell_w, grid_h,
            {th.accent.r, th.accent.g, th.accent.b, 1.0f}, 0.18f);
    }

    // Row separator line between value and gate rows.
    if (d.draw_rect) {
        d.draw_rect(o, grid_x, grid_y + cell_h - 0.5f, grid_w, 1.0f,
                    {th.separator.r, th.separator.g, th.separator.b, 0.75f});
    }

    // Cells.
    for (int step = 0; step < se::kMaxSteps; ++step) {
        const float cx = grid_x + step * cell_w;
        const bool beyond = (step >= num_steps);
        const float pad = 2.0f;
        if (cell_w <= pad * 2.0f || cell_h <= pad * 2.0f) continue;

        // --- Row 0: value fader ---
        {
            const float ix = cx + pad;
            const float iy = grid_y + pad;
            const float iw = std::max(0.0f, cell_w - 2.0f * pad);
            const float ih = std::max(0.0f, cell_h - 2.0f * pad);
            const VividColor bg{0.12f, 0.13f, 0.15f, beyond ? 0.35f : 0.9f};
            vivid::draw_ui::draw_panel(d, o, ix, iy, iw, ih, bg,
                {0, 0, 0, 0}, 2.0f);
            if (!beyond) {
                const float val = std::clamp(get_param(
                    se::param_index_for(se::RowKind::Value, step), 0.5f),
                    0.0f, 1.0f);
                const VividColor fill{
                    th.accent.r, th.accent.g, th.accent.b, 0.7f};
                if (bipolar) {
                    // Centre-origin meter: fill from midline to value.
                    const float mid = iy + ih * 0.5f;
                    const float dv  = (val - 0.5f) * 2.0f;  // -1..+1
                    const float bar_h = std::abs(dv) * (ih * 0.5f);
                    const float bar_y = (dv >= 0.0f) ? (mid - bar_h) : mid;
                    if (bar_h > 0.0f && d.draw_rect)
                        d.draw_rect(o, ix + 1.0f, bar_y,
                                    std::max(0.0f, iw - 2.0f), bar_h, fill);
                    if (d.draw_rect)
                        d.draw_rect(o, ix + 1.0f, mid - 0.5f,
                                    std::max(0.0f, iw - 2.0f), 1.0f,
                                    {th.dim_text.r, th.dim_text.g,
                                     th.dim_text.b, 0.4f});
                } else {
                    const float bar_h = ih * val;
                    if (bar_h > 0.0f && d.draw_rect)
                        d.draw_rect(o, ix + 1.0f, iy + ih - bar_h,
                                    std::max(0.0f, iw - 2.0f), bar_h, fill);
                }
            }
        }

        // --- Row 1: gate toggle ---
        {
            const float ix = cx + pad;
            const float iy = grid_y + cell_h + pad;
            const float iw = std::max(0.0f, cell_w - 2.0f * pad);
            const float ih = std::max(0.0f, cell_h - 2.0f * pad);
            const VividColor bg{0.12f, 0.13f, 0.15f, beyond ? 0.35f : 0.9f};
            vivid::draw_ui::draw_panel(d, o, ix, iy, iw, ih, bg,
                {0, 0, 0, 0}, 2.0f);
            if (!beyond) {
                const float g = get_param(
                    se::param_index_for(se::RowKind::Gate, step), 1.0f);
                if (g > 0.5f) {
                    const VividColor fill{
                        th.accent.r * 0.7f, th.accent.g * 0.9f,
                        th.accent.b * 1.1f, 0.85f};
                    vivid::draw_ui::draw_panel(d, o, ix, iy, iw, ih, fill,
                        {0, 0, 0, 0}, 2.0f);
                }
            }
        }
    }

    // Selection + cursor outlines.
    if (cell_w > 0.0f && cell_h > 0.0f) {
        const auto& sel = editor_selection_;
        const float sx = grid_x + sel.col_lo * cell_w;
        const float sy = grid_y + sel.row_lo * cell_h;
        const float sw = (sel.col_hi - sel.col_lo + 1) * cell_w;
        const float sh = (sel.row_hi - sel.row_lo + 1) * cell_h;
        vivid::draw_ui::draw_panel(d, o, sx, sy, sw, sh,
            {0, 0, 0, 0},
            {th.accent.r, th.accent.g, th.accent.b, 0.9f},
            0.0f, 1.0f);

        const float ccx = grid_x + editor_cursor_step_ * cell_w;
        const float ccy = grid_y + editor_cursor_row_  * cell_h;
        vivid::draw_ui::draw_panel(d, o, ccx, ccy, cell_w, cell_h,
            {0, 0, 0, 0},
            {th.bright_text.r, th.bright_text.g, th.bright_text.b, 1.0f},
            0.0f, 1.5f);
    }

    // ---- Side panel: cursor readout + selection summary ----
    constexpr float kSpPad = 10.0f;
    vivid::draw_ui::draw_panel(d, o, side_x, side_y, side_w, side_h,
        {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 0.85f},
        {th.separator.r, th.separator.g, th.separator.b, 0.8f}, 4.0f, 1.0f);

    if (d.draw_text) {
        const int cell_count = se::selection_cell_count(editor_selection_);
        const char* row_label = (editor_cursor_row_ == 0) ? "value" : "gate";
        char hdr[96];
        if (cell_count == 1) {
            std::snprintf(hdr, sizeof(hdr), "%s · step %d",
                row_label, editor_cursor_step_ + 1);
        } else {
            std::snprintf(hdr, sizeof(hdr),
                "rows %d-%d · steps %d-%d  (%d cells)",
                editor_selection_.row_lo, editor_selection_.row_hi,
                editor_selection_.col_lo + 1, editor_selection_.col_hi + 1,
                cell_count);
        }
        d.draw_text(o, side_x + kSpPad, side_y + kSpPad, hdr,
            {th.bright_text.r, th.bright_text.g,
             th.bright_text.b, 0.95f}, 1.0f);

        // Cursor cell value + gate readout.
        const float cur_val = get_param(
            se::param_index_for(se::RowKind::Value, editor_cursor_step_), 0.5f);
        const float cur_gate = get_param(
            se::param_index_for(se::RowKind::Gate, editor_cursor_step_), 1.0f);
        char line[80];
        std::snprintf(line, sizeof(line), "value: %.2f", cur_val);
        d.draw_text(o, side_x + kSpPad, side_y + kSpPad + 24.0f, line,
            {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.9f}, 1.0f);
        std::snprintf(line, sizeof(line), "gate:  %s",
            cur_gate > 0.5f ? "on" : "off");
        d.draw_text(o, side_x + kSpPad, side_y + kSpPad + 42.0f, line,
            {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.9f}, 1.0f);
    }
}
