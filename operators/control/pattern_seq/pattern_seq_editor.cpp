// Dedicated editor window for PatternSeq. Single-row, 16-column
// bipolar-fader grid; values run ±10000 with zero at the cell midline.
// Top bar carries the read-only top-level controls; side panel offers
// quick-fill buttons (ramp up, ramp down, zero, random).
//
// Smaller than Sequencer (no gate lane) and simpler than DrumSequencer
// (no pattern banks, no probability per step). Written around the
// shared vivid::ui::Selection + ui_step_grid + editor_keys vocabulary.

#include "pattern_seq_core.h"
#include "pattern_seq_editor_shared.h"
#include "operator_api/draw_ui_helpers.h"
#include "operator_api/editor_keys.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace ps_ed {

constexpr float kInset      = 8.0f;
constexpr float kTopBarH    = 28.0f;
constexpr float kSidePanelW = 220.0f;

// Side-panel quick-fill button layout.
constexpr float kBtnH   = 22.0f;
constexpr float kBtnGap = 4.0f;

} // namespace ps_ed


VividEditorMetadata PatternSeqCore::editor_metadata() {
    VividEditorMetadata m{};
    m.default_width  = 880;
    m.default_height = 320;
    m.min_width      = 600;
    m.min_height     = 240;
    m.title_suffix   = "PatternSeq Editor";
    return m;
}

void PatternSeqCore::draw_editor(VividEditorContext* ctx) {
    if (!ctx) return;
    namespace ed  = ::ps_ed;
    namespace ek  = ::vivid::editor_keys;
    namespace ps  = ::vivid::pattern_seq_editor;
    namespace ui  = ::vivid::ui;
    namespace sel = ::vivid::ui;

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
    auto set_step = [&](int step, float v) {
        set_named(ps::param_name_for(step).c_str(), ps::clamp_value(v));
    };

    // ---- Live state ----
    const int num_steps = std::clamp(
        static_cast<int>(std::lround(get_param(ps::kStepsIndex, 8.0f))),
        1, ps::kMaxSteps);
    const int current_step = (ctx->output_count > ps::kStepOutputIndex)
        ? static_cast<int>(ctx->output_values[ps::kStepOutputIndex]) : -1;
    const float gate_len = std::clamp(get_param(ps::kGateLengthIndex, 0.8f),
                                      0.0f, 1.0f);
    const float prob     = std::clamp(get_param(ps::kProbabilityIndex, 1.0f),
                                      0.0f, 1.0f);

    // ---- Clamp persistent state into current bounds ----
    if (editor_cursor_step_ > num_steps - 1) editor_cursor_step_ = num_steps - 1;
    if (editor_cursor_step_ < 0) editor_cursor_step_ = 0;
    if (grid_state_.anchor_col > num_steps - 1) grid_state_.anchor_col = num_steps - 1;
    if (grid_state_.anchor_col < 0) grid_state_.anchor_col = 0;
    grid_state_.anchor_row = 0;
    editor_selection_.row_lo = 0;
    editor_selection_.row_hi = 0;
    if (editor_selection_.col_lo < 0) editor_selection_.col_lo = 0;
    if (editor_selection_.col_hi > num_steps - 1)
        editor_selection_.col_hi = num_steps - 1;
    if (editor_selection_.col_hi < editor_selection_.col_lo)
        editor_selection_.col_hi = editor_selection_.col_lo;

    auto rebuild_selection = [&]() {
        editor_selection_ = sel::selection_from_anchor_tip(
            0, grid_state_.anchor_col, 0, editor_cursor_step_);
    };
    auto collapse_selection_to_cursor = [&]() {
        grid_state_.anchor_col = editor_cursor_step_;
        rebuild_selection();
    };
    rebuild_selection();

    auto for_each_selected = [&](auto&& fn) {
        for (int s = editor_selection_.col_lo; s <= editor_selection_.col_hi; ++s)
            fn(s);
    };

    auto set_selection_value = [&](float v) {
        for_each_selected([&](int s) { set_step(s, v); });
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

    const ui::Rect grid_bounds{grid_x, grid_y, grid_w, grid_h};
    const float cell_w = (grid_w > 0.0f)
        ? grid_w / static_cast<float>(ps::kMaxSteps) : 0.0f;

    // ---- Quick-fill helpers ----
    auto apply_fill = [&](void (*gen)(float*, int)) {
        float buf[ps::kMaxSteps] = {};
        gen(buf, num_steps);
        for (int s = 0; s < num_steps; ++s) set_step(s, buf[s]);
    };
    auto apply_fill_random = [&]() {
        float buf[ps::kMaxSteps] = {};
        // Seed from editor time + cursor so repeated presses vary.
        const std::uint32_t seed =
            static_cast<std::uint32_t>(ctx->time * 1e6) ^
            static_cast<std::uint32_t>(editor_cursor_step_ + 0x51D15EED);
        ps::fill_random(buf, num_steps, seed);
        for (int s = 0; s < num_steps; ++s) set_step(s, buf[s]);
    };

    // ---- Keyboard ----
    ctx->wants_keyboard = 1;
    for (uint32_t ei = 0; ei < ctx->event_count; ++ei) {
        const auto& e = ctx->events[ei];
        if (e.type != VIVID_EDITOR_EVENT_KEY) continue;
        if (e.action != ek::kPress && e.action != ek::kRepeat) continue;

        const bool shift = (e.modifiers & ek::kModShift) != 0;
        const bool cmd_or_ctrl = ek::is_cmd_or_ctrl(e.modifiers);

        if (e.key == ek::kLeft || e.key == ek::kRight) {
            const int dx = (e.key == ek::kRight) ? +1 : -1;
            if (shift && editor_selection_.col_lo == editor_selection_.col_hi)
                grid_state_.anchor_col = editor_cursor_step_;
            editor_cursor_step_ = std::clamp(
                editor_cursor_step_ + dx, 0, num_steps - 1);
            if (shift) rebuild_selection();
            else       collapse_selection_to_cursor();
            continue;
        }
        if (e.key == ek::kHome) {
            editor_cursor_step_ = 0;
            if (shift) rebuild_selection(); else collapse_selection_to_cursor();
            continue;
        }
        if (e.key == ek::kEnd) {
            editor_cursor_step_ = num_steps - 1;
            if (shift) rebuild_selection(); else collapse_selection_to_cursor();
            continue;
        }
        // Up/Down nudge the cursor-cell value (not cursor position).
        if (e.key == ek::kUp || e.key == ek::kDown) {
            const float step = shift ? 1000.0f : 100.0f;
            const float dv = (e.key == ek::kUp) ? +step : -step;
            for_each_selected([&](int s) {
                const float cur = get_param(ps::param_index_for(s), 0.0f);
                set_step(s, cur + dv);
            });
            continue;
        }
        if (e.key == ek::kSpace || e.key == ek::kZ) {
            set_selection_value(0.0f);
            continue;
        }
        if (e.key == ek::kR && !cmd_or_ctrl) {
            apply_fill(ps::fill_ramp_up);
            continue;
        }
        // Digits map to fractional (bipolar) values:
        //   '0' = 0
        //   '1'..'9' = k * 12.5% of max  (so '5' = 62.5% of max)
        // Negative values are the second half: digits 5..9 become
        // negative via Shift. Simpler for now: just positive.
        if (!cmd_or_ctrl && ek::is_digit_key(e.key)) {
            const int digit = ek::digit_value(e.key);
            const float v = (digit == 0) ? 0.0f
                : static_cast<float>(digit) / 9.0f * ps::kValueMax;
            set_selection_value(shift ? -v : v);
            continue;
        }
        // Cmd+C / Cmd+V: rectangular 1×N copy/paste.
        if (cmd_or_ctrl && e.key == ek::kC) {
            const int cols = editor_selection_.col_hi - editor_selection_.col_lo + 1;
            if (cols > 0 && cols <= ps::kMaxSteps) {
                editor_clipboard_.cols = cols;
                for (int c = 0; c < cols; ++c) {
                    const int step = editor_selection_.col_lo + c;
                    editor_clipboard_.values[c] =
                        get_param(ps::param_index_for(step), 0.0f);
                }
                editor_clipboard_.has_content = true;
            }
            continue;
        }
        if (cmd_or_ctrl && e.key == ek::kV) {
            if (editor_clipboard_.has_content) {
                const int origin = editor_selection_.col_lo;
                for (int c = 0; c < editor_clipboard_.cols; ++c) {
                    const int step = origin + c;
                    if (step < 0 || step >= num_steps) continue;
                    set_step(step, editor_clipboard_.values[c]);
                }
            }
            continue;
        }
        if (e.key == ek::kEscape) {
            collapse_selection_to_cursor();
            continue;
        }
    }

    // ---- Mouse: ui_step_grid handles click / drag / shift-extend ----
    auto grid_res = ui::ui_step_grid(*ctx, grid_bounds,
                                     /*rows=*/1, ps::kMaxSteps,
                                     num_steps, &grid_state_);
    if (grid_res.shift_extending) {
        editor_cursor_step_ = grid_res.tip_col;
        rebuild_selection();
    } else if (grid_res.cell_clicked) {
        editor_cursor_step_ = grid_res.clicked_col;
        if (!grid_res.clicked_with_shift) collapse_selection_to_cursor();
    }
    if (grid_res.drag_painting &&
        grid_res.drag_col >= 0 && grid_res.drag_col < num_steps) {
        const float v = ps::value_from_cell_y(grid_res.drag_mouse_y_in_cell);
        set_step(grid_res.drag_col, v);
    }

    // ---- Side panel: quick-fill buttons ----
    const auto& mouse = ctx->mouse;
    constexpr float kSpPad = 10.0f;
    const float btn_y0 = side_y + 120.0f;
    struct Btn { const char* label; int action; };
    const Btn buttons[] = {
        {"Ramp ↗",  0},
        {"Ramp ↘",  1},
        {"Zero",    2},
        {"Random",  3},
    };

    // ---- Drawing ----
    vivid::draw_ui::draw_panel(d, o, grid_x, grid_y, grid_w, grid_h,
        {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 0.92f});

    // Top bar.
    if (d.draw_text) {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
            "Steps %d   Gate %.2f   Prob %.2f",
            num_steps, gate_len, prob);
        d.draw_text(o, grid_x, top_y + 6.0f, buf,
            {th.bright_text.r, th.bright_text.g,
             th.bright_text.b, 0.95f}, 1.0f);

        const char* hints =
            "drag=value  ·  ←/→ nav  ·  ↑/↓ nudge  ·  "
            "Shift+↑/↓ coarse  ·  0-9 set  ·  R ramp  ·  Z/Space zero";
        const float scale = 0.7f;
        const float hints_w = d.text_width
            ? d.text_width(o, hints, scale) : 540.0f;
        d.draw_text(o, grid_x + grid_w - hints_w, top_y + 8.0f, hints,
            {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.7f}, scale);
    }

    // Beat markers every 4 steps.
    if (d.draw_rect) {
        for (int s = 4; s < ps::kMaxSteps; s += 4) {
            const float x = grid_x + s * cell_w - 0.5f;
            d.draw_rect(o, x, grid_y, 1.0f, grid_h,
                {th.separator.r, th.separator.g, th.separator.b, 0.45f});
        }
    }

    // Current step highlight.
    if (current_step >= 0 && current_step < num_steps && cell_w > 0.0f) {
        vivid::draw_ui::draw_selection_highlight(d, o,
            grid_x + current_step * cell_w, grid_y, cell_w, grid_h,
            {th.accent.r, th.accent.g, th.accent.b, 1.0f}, 0.18f);
    }

    // Cells — bipolar fill: centre midline, fill upward for +v or
    // downward for -v.
    for (int s = 0; s < ps::kMaxSteps; ++s) {
        const float cx = grid_x + s * cell_w;
        const bool beyond = (s >= num_steps);
        const float pad = 2.0f;
        if (cell_w <= pad * 2.0f || grid_h <= pad * 2.0f) continue;

        const float ix = cx + pad;
        const float iy = grid_y + pad;
        const float iw = std::max(0.0f, cell_w - 2.0f * pad);
        const float ih = std::max(0.0f, grid_h - 2.0f * pad);

        // Cell backdrop.
        vivid::draw_ui::draw_panel(d, o, ix, iy, iw, ih,
            {0.12f, 0.13f, 0.15f, beyond ? 0.35f : 0.9f},
            {0, 0, 0, 0}, 2.0f);

        if (!beyond) {
            const float v = std::clamp(
                get_param(ps::param_index_for(s), 0.0f),
                ps::kValueMin, ps::kValueMax);
            const float norm = v / ps::kValueMax;       // -1..+1
            const float mid  = iy + ih * 0.5f;
            const float bar  = std::abs(norm) * (ih * 0.5f);
            const float by   = (norm >= 0.0f) ? (mid - bar) : mid;
            const VividColor fill{
                th.accent.r, th.accent.g, th.accent.b, 0.72f};
            if (bar > 0.0f && d.draw_rect) {
                d.draw_rect(o, ix + 1.0f, by,
                    std::max(0.0f, iw - 2.0f), bar, fill);
            }
            // Midline tick.
            if (d.draw_rect) {
                d.draw_rect(o, ix + 1.0f, mid - 0.5f,
                    std::max(0.0f, iw - 2.0f), 1.0f,
                    {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.4f});
            }
        }
    }

    // Selection + cursor outlines.
    if (cell_w > 0.0f && grid_h > 0.0f) {
        const auto& selr = editor_selection_;
        const float sx = grid_x + selr.col_lo * cell_w;
        const float sw = (selr.col_hi - selr.col_lo + 1) * cell_w;
        vivid::draw_ui::draw_panel(d, o, sx, grid_y, sw, grid_h,
            {0, 0, 0, 0},
            {th.accent.r, th.accent.g, th.accent.b, 0.9f}, 0.0f, 1.0f);

        const float ccx = grid_x + editor_cursor_step_ * cell_w;
        vivid::draw_ui::draw_panel(d, o, ccx, grid_y, cell_w, grid_h,
            {0, 0, 0, 0},
            {th.bright_text.r, th.bright_text.g, th.bright_text.b, 1.0f},
            0.0f, 1.5f);
    }

    // ---- Side panel ----
    vivid::draw_ui::draw_panel(d, o, side_x, side_y, side_w, side_h,
        {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 0.85f},
        {th.separator.r, th.separator.g, th.separator.b, 0.8f}, 4.0f, 1.0f);

    if (d.draw_text) {
        char line[96];
        // Cursor readout.
        const float cur_val = get_param(
            ps::param_index_for(editor_cursor_step_), 0.0f);
        std::snprintf(line, sizeof(line), "Step %d", editor_cursor_step_ + 1);
        d.draw_text(o, side_x + kSpPad, side_y + kSpPad, line,
            {th.bright_text.r, th.bright_text.g,
             th.bright_text.b, 0.95f}, 1.0f);
        std::snprintf(line, sizeof(line), "value: %.1f", cur_val);
        d.draw_text(o, side_x + kSpPad, side_y + kSpPad + 22.0f, line,
            {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.9f}, 1.0f);

        const int sel_cols = editor_selection_.col_hi - editor_selection_.col_lo + 1;
        if (sel_cols > 1) {
            std::snprintf(line, sizeof(line),
                "selection: steps %d..%d (%d)",
                editor_selection_.col_lo + 1,
                editor_selection_.col_hi + 1, sel_cols);
            d.draw_text(o, side_x + kSpPad, side_y + kSpPad + 42.0f, line,
                {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.85f}, 0.85f);
        }

        // Quick-fill section.
        d.draw_text(o, side_x + kSpPad, btn_y0 - 22.0f, "Quick fill",
            {th.bright_text.r, th.bright_text.g,
             th.bright_text.b, 0.9f}, 1.0f);
    }
    // Quick-fill buttons — draw + click in one pass via ui_icon_button.
    for (int i = 0; i < 4; ++i) {
        const float by = btn_y0 + static_cast<float>(i) * (ed::kBtnH + ed::kBtnGap);
        const vivid::ui::Rect br{side_x + kSpPad, by,
                                  side_w - 2.0f * kSpPad, ed::kBtnH};
        auto btn = vivid::ui::ui_icon_button(*ctx, br, buttons[i].label);
        if (btn.clicked) {
            switch (buttons[i].action) {
                case 0: apply_fill(ps::fill_ramp_up);   break;
                case 1: apply_fill(ps::fill_ramp_down); break;
                case 2: apply_fill(ps::fill_zero);      break;
                case 3: apply_fill_random();             break;
            }
        }
    }
}
