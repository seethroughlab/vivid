// Dedicated editor window for DrumSequencer.
//
// Surface is split into a left-side unified grid (pattern-A or pattern-B
// triggers + per-cell velocity, probability bar, roll digit) and a
// right-side 280 px "inspector" panel that reflects the current selection
// and lets the user edit velocity / mod-B / probability / roll / trigger
// state for every cell at once. A top bar shows the step count, the
// pattern-A/B toggle, and compact keyboard hints.
//
// Selection model: anchor + cursor. Arrow keys move the cursor and
// collapse the selection to a single cell; shift+arrow extends by
// keeping the anchor fixed. Mouse click sets anchor = cursor; shift+click
// extends. Cmd+C / Cmd+V copy and paste the rectangular selection.

#include "drum_sequencer_core.h"
#include "drum_sequencer_editor_shared.h"
#include "operator_api/draw_ui_helpers.h"
#include "operator_api/editor_keys.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

namespace drum_ed {

constexpr float kTopBarH     = 26.0f;
constexpr float kInset       = 8.0f;
constexpr float kSidePanelW  = 280.0f;
constexpr float kLabelW      = 28.0f;
constexpr float kSideRowGap  = 4.0f;

// Per-drum row colours — reused from the inspector/thumbnail for
// visual continuity.
constexpr float kDrumColors[6][3] = {
    {0.86f, 0.31f, 0.31f}, {0.86f, 0.75f, 0.24f}, {0.24f, 0.78f, 0.71f},
    {0.31f, 0.51f, 0.86f}, {0.63f, 0.35f, 0.78f}, {0.31f, 0.78f, 0.39f},
};

// (Grid drag state now lives inside vivid::ui::GridState on the core —
// Phase B of the editor-UI platform plan retired the integer lane codes.)

} // namespace drum_ed

VividEditorMetadata DrumSequencerCore::editor_metadata() {
    VividEditorMetadata m{};
    m.default_width  = 1100;
    m.default_height = 600;
    m.min_width      = 820;
    m.min_height     = 440;
    m.title_suffix   = "DrumSequencer Editor";
    return m;
}

void DrumSequencerCore::draw_editor(VividEditorContext* ctx) {
    if (!ctx) return;
    namespace ed     = ::drum_ed;
    namespace ek     = ::vivid::editor_keys;
    namespace de     = ::vivid_sequencers::drum_editor;
    namespace layout = ::vivid_sequencers::drum_layout;

    auto& d = ctx->draw;
    void* o = d.opaque;
    const auto& th = ctx->theme;

    // ------------------------------------------------------------
    // Live-param lookup helpers
    // ------------------------------------------------------------
    auto get_param = [&](int idx, float fallback) -> float {
        if (idx < 0) return fallback;
        if (static_cast<uint32_t>(idx) >= ctx->param_count) return fallback;
        return ctx->param_values[idx];
    };
    auto set_named = [&](const char* name, float v) {
        if (ctx->commands.set_param)
            ctx->commands.set_param(ctx->commands.opaque, name, v);
    };
    auto set_lane = [&](de::LaneKind lane, std::size_t drum, int step, float v) {
        set_named(de::param_name_for(lane, drum, step).c_str(), v);
    };

    int num_steps = 16;
    if (ctx->param_count > 0) {
        num_steps = std::clamp(static_cast<int>(ctx->param_values[0]), 1,
                               static_cast<int>(layout::kStepCount));
    }
    const int current_step = (ctx->output_count > layout::kStepOutputIndex)
        ? static_cast<int>(ctx->output_values[layout::kStepOutputIndex]) : -1;
    const int active_ptn = std::clamp(
        static_cast<int>(get_param(layout::kActivePatternIndex, 0.0f)),
        0, static_cast<int>(layout::kPatternCount) - 1);
    const de::LaneKind trigger_lane = de::lane_for_pattern(active_ptn);

    // Song mode and the live playing pattern are read live from the
    // operator's params and outputs. In manual mode the playing pattern
    // equals active_ptn; in song mode it follows the song_pos_ state
    // emitted to current_pattern.
    const bool song_on = get_param(layout::kSongModeIndex, 0.0f) > 0.5f;
    const int playing_ptn = (ctx->output_count > layout::kCurrentPatternOutputIndex)
        ? std::clamp(static_cast<int>(
              ctx->output_values[layout::kCurrentPatternOutputIndex]),
              0, static_cast<int>(layout::kPatternCount) - 1)
        : active_ptn;

    // ------------------------------------------------------------
    // Sanitise persistent state (num_steps could have shrunk since
    // the selection was stored). Shared helper clamps cursor, anchor,
    // and the cached selection rectangle in one pass.
    // ------------------------------------------------------------
    de::clamp_editor_state(static_cast<int>(layout::kDrumCount) - 1,
                           num_steps - 1,
                           &editor_cursor_drum_, &editor_cursor_step_,
                           &grid_state_.anchor_row, &grid_state_.anchor_col,
                           &editor_selection_);

    auto rebuild_selection = [&]() {
        editor_selection_ = de::selection_from_anchor_tip(
            grid_state_.anchor_row, grid_state_.anchor_col,
            editor_cursor_drum_,   editor_cursor_step_);
    };
    auto collapse_selection_to_cursor = [&]() {
        grid_state_.anchor_row = editor_cursor_drum_;
        grid_state_.anchor_col = editor_cursor_step_;
        rebuild_selection();
    };

    // First-time init: rebuild selection from whatever anchor/cursor we
    // have so the cached rect is self-consistent.
    rebuild_selection();

    // ------------------------------------------------------------
    // Selection-wide editing operations
    // ------------------------------------------------------------
    auto for_each_selected = [&](auto&& fn) {
        for (int dd = editor_selection_.row_lo;
             dd <= editor_selection_.row_hi; ++dd) {
            for (int ss = editor_selection_.col_lo;
                 ss <= editor_selection_.col_hi; ++ss) {
                fn(static_cast<std::size_t>(dd), ss);
            }
        }
    };

    auto toggle_trigger_selection = [&]() {
        // Probe the cursor cell. If triggered → turn all OFF; else → ON.
        const int cidx = de::param_index_for(trigger_lane,
            static_cast<std::size_t>(editor_cursor_drum_), editor_cursor_step_);
        const float cur = get_param(cidx, 0.0f);
        const float target = (cur > 0.5f) ? 0.0f : 1.0f;
        for_each_selected([&](std::size_t dd, int ss) {
            set_lane(trigger_lane, dd, ss, target);
        });
    };
    auto clear_selection_values = [&]() {
        for_each_selected([&](std::size_t dd, int ss) {
            set_lane(de::LaneKind::Pattern,     dd, ss, 0.0f);
            set_lane(de::LaneKind::PatternB,    dd, ss, 0.0f);
            set_lane(de::LaneKind::ModA,        dd, ss, 0.5f);
            set_lane(de::LaneKind::ModB,        dd, ss, 0.5f);
            set_lane(de::LaneKind::Probability, dd, ss, 1.0f);
            set_lane(de::LaneKind::Roll,        dd, ss, 1.0f);
        });
    };
    auto set_roll_selection = [&](int rc) {
        const float v = static_cast<float>(std::clamp(rc, 1, 4));
        for_each_selected([&](std::size_t dd, int ss) {
            set_lane(de::LaneKind::Roll, dd, ss, v);
        });
    };
    auto set_probability_selection = [&](float p) {
        p = std::clamp(p, 0.0f, 1.0f);
        for_each_selected([&](std::size_t dd, int ss) {
            set_lane(de::LaneKind::Probability, dd, ss, p);
        });
    };
    auto set_velocity_selection = [&](float v) {
        v = std::clamp(v, 0.0f, 1.0f);
        for_each_selected([&](std::size_t dd, int ss) {
            set_lane(de::LaneKind::ModA, dd, ss, v);
        });
    };
    auto set_mod_b_selection = [&](float v) {
        v = std::clamp(v, 0.0f, 1.0f);
        for_each_selected([&](std::size_t dd, int ss) {
            set_lane(de::LaneKind::ModB, dd, ss, v);
        });
    };

    // ------------------------------------------------------------
    // Layout
    // ------------------------------------------------------------
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

    const de::GridMetrics gm = de::compute_grid_metrics(
        grid_x, grid_y, grid_w, grid_h, ed::kLabelW);

    // Side-panel widgets (Phase A of the editor-UI platform plan) compute
    // their hit rects at the point of use via ui_layout + ui_row / split_h.
    // Top-bar pattern-A/B buttons likewise fold into a pair of ui_toggle
    // calls during the top-bar drawing pass below.

    // ------------------------------------------------------------
    // Keyboard event handling
    // ------------------------------------------------------------
    ctx->wants_keyboard = 1;
    for (uint32_t ei = 0; ei < ctx->event_count; ++ei) {
        const auto& e = ctx->events[ei];
        if (e.type != VIVID_EDITOR_EVENT_KEY) continue;
        if (e.action != ek::kPress && e.action != ek::kRepeat) continue;

        const bool shift = (e.modifiers & ek::kModShift) != 0;
        const bool cmd_or_ctrl = ek::is_cmd_or_ctrl(e.modifiers);

        // --- Two-step "P <digit>" probability shortcut ---
        if (editor_prob_prefix_mode_) {
            if (ek::is_digit_key(e.key)) {
                const int digit = ek::digit_value(e.key);
                set_probability_selection(static_cast<float>(digit) / 10.0f);
            }
            // Any key (digit or not) exits prefix mode.
            editor_prob_prefix_mode_ = false;
            continue;
        }

        // --- Arrow keys ---
        int dx = 0, dy = 0;
        if      (e.key == ek::kUp)    dy = -1;
        else if (e.key == ek::kDown)  dy = +1;
        else if (e.key == ek::kLeft)  dx = -1;
        else if (e.key == ek::kRight) dx = +1;
        if (dx != 0 || dy != 0) {
            de::cursor_move(dx, dy,
                            static_cast<int>(layout::kDrumCount) - 1,
                            num_steps - 1,
                            &editor_cursor_drum_, &editor_cursor_step_);
            if (shift) {
                rebuild_selection();
            } else {
                collapse_selection_to_cursor();
            }
            continue;
        }

        // --- Action keys ---
        if (e.key == ek::kEnter) {
            toggle_trigger_selection();
        } else if (e.key == ek::kSpace) {
            clear_selection_values();
        } else if (e.key >= (ek::k0 + 1) && e.key <= (ek::k0 + 4)) {
            // Roll shortcut: 1/2/3/4 set ratchet count across the selection.
            set_roll_selection(ek::digit_value(e.key));
        } else if (e.key == ek::kP) {
            editor_prob_prefix_mode_ = true;
        } else if (e.key == ek::kEscape) {
            // Escape collapses the selection back to a single cell.
            collapse_selection_to_cursor();
        } else if (e.key == ek::kA && !cmd_or_ctrl) {
            set_named("active_pattern", 0.0f);
        } else if (e.key == ek::kB && !cmd_or_ctrl) {
            set_named("active_pattern", 1.0f);
        } else if (e.key == ek::kC && !cmd_or_ctrl) {
            set_named("active_pattern", 2.0f);
        } else if (e.key == ek::kD && !cmd_or_ctrl) {
            set_named("active_pattern", 3.0f);
        } else if (e.key == ek::kS && !cmd_or_ctrl) {
            // Toggle song mode. Reads live so the next press flips back.
            const bool now_on = get_param(layout::kSongModeIndex, 0.0f) > 0.5f;
            set_named("song_mode", now_on ? 0.0f : 1.0f);
        } else if (e.key == ek::kC && cmd_or_ctrl) {
            de::copy_selection(ctx->param_values, ctx->param_count,
                               editor_selection_, &selection_clipboard_);
        } else if (e.key == ek::kV && cmd_or_ctrl) {
            de::paste_selection(ctx->commands, selection_clipboard_,
                                static_cast<std::size_t>(editor_selection_.row_lo),
                                editor_selection_.col_lo);
        }
    }

    // ------------------------------------------------------------
    // Drawing
    // ------------------------------------------------------------
    // Backdrop: editor window already has a clear; extra dark bg panel
    // under the grid helps the cells read against the window chrome.
    vivid::draw_ui::draw_panel(d, o, grid_x, grid_y, grid_w, grid_h,
        {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 0.9f});

    // --- Top bar text + pattern buttons ---
    if (d.draw_text) {
        char steps_buf[48];
        std::snprintf(steps_buf, sizeof(steps_buf), "Steps: %d", num_steps);
        d.draw_text(o, grid_x, top_y + 4.0f, steps_buf,
                    {th.bright_text.r, th.bright_text.g, th.bright_text.b, 0.9f},
                    1.0f);
        d.draw_text(o, grid_x + 80.0f, top_y + 4.0f, "Pattern",
                    {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.9f}, 1.0f);
    }
    float pattern_block_end = grid_x + 140.0f;
    {
        constexpr const char* kPatternBtnLabels[] = {"A", "B", "C", "D"};
        constexpr int kPatternCount = static_cast<int>(layout::kPatternCount);
        constexpr float kTabW = 28.0f;
        const vivid::ui::Rect strip_r{
            grid_x + 140.0f, top_y + 3.0f,
            kTabW * kPatternCount, top_h - 6.0f};
        auto tab = vivid::ui::ui_tab_strip(*ctx, strip_r,
            kPatternBtnLabels, kPatternCount, active_ptn);
        if (tab.clicked)
            set_named("active_pattern", static_cast<float>(tab.clicked_idx));
        // Live-playing LED overlaid on the playing pattern's tab.
        if (song_on && playing_ptn >= 0 && playing_ptn < kPatternCount
                && d.draw_rounded_rect) {
            const float dot_w = 6.0f;
            const float dx = strip_r.x + playing_ptn * kTabW + kTabW - dot_w - 1.0f;
            const float dy = strip_r.y + 1.0f;
            d.draw_rounded_rect(o, dx, dy, dot_w, dot_w, 3.0f,
                {0.95f, 0.85f, 0.25f, 0.95f});
        }
        pattern_block_end = strip_r.x + strip_r.w;

        // Song toggle: 36 px wide, 8 px gap after the pattern strip.
        const VividColor fill_off{0.18f, 0.18f, 0.21f, 1.0f};
        const float song_x = pattern_block_end + 8.0f;
        const float song_w = 36.0f;
        const vivid::ui::Rect song_r{song_x, top_y + 3.0f, song_w, top_h - 6.0f};
        const VividColor song_fill_on{0.95f, 0.65f, 0.25f, 1.0f};
        auto song_btn = vivid::ui::ui_toggle(*ctx, song_r, "Song",
                                             song_on, fill_off, song_fill_on);
        if (song_btn.clicked)
            set_named("song_mode", song_on ? 0.0f : 1.0f);
        pattern_block_end = song_x + song_w;
    }

    // Keyboard hints, right-aligned in top bar. Suppressed when the pattern
    // button row is wide enough to collide with the hint string.
    if (d.draw_text) {
        const char* hints =
            "Enter=trigger  Space=clear  1-4=roll  P<digit>=prob  S=song  Shift+Arrow=extend  Cmd+C/V";
        const float hints_scale = 0.75f;
        const float hints_w = d.text_width
            ? d.text_width(o, hints, hints_scale) : 420.0f;
        const float hints_x = grid_x + grid_w - hints_w;
        if (hints_x > pattern_block_end + 16.0f) {
            d.draw_text(o, hints_x, top_y + 6.0f, hints,
                {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.75f},
                hints_scale);
        }
    }

    // --- Grid drum-row labels ---
    for (std::size_t drum = 0; drum < layout::kDrumCount; ++drum) {
        const float row_y_label = gm.cells_y + drum * gm.cell_h;
        if (d.draw_text) {
            d.draw_text(o, gm.origin_x + 2.0f,
                        row_y_label + gm.cell_h * 0.5f - 6.0f,
                        layout::kDrumLabels[drum],
                        {ed::kDrumColors[drum][0], ed::kDrumColors[drum][1],
                         ed::kDrumColors[drum][2], 0.9f}, 1.0f);
        }
    }

    // Beat-group separators every 4 steps.
    for (int b = 1; b < 4; ++b) {
        const float sx = gm.cells_x + b * 4 * gm.cell_w;
        if (d.draw_rect) {
            d.draw_rect(o, sx - 0.5f, gm.cells_y, 1.0f, gm.grid_h,
                        {th.separator.r, th.separator.g, th.separator.b, 0.6f});
        }
    }

    // Current-step column highlight.
    if (current_step >= 0 && current_step < num_steps && gm.cell_w > 0.0f) {
        vivid::draw_ui::draw_selection_highlight(
            d, o,
            gm.cells_x + current_step * gm.cell_w, gm.cells_y,
            gm.cell_w, gm.grid_h,
            {th.accent.r, th.accent.g, th.accent.b, 1.0f}, 0.18f);
    }

    // --- Cells ---
    for (std::size_t drum = 0; drum < layout::kDrumCount; ++drum) {
        for (int s = 0; s < static_cast<int>(layout::kStepCount); ++s) {
            const float cx = gm.cells_x + s * gm.cell_w;
            const float cy = gm.cells_y + drum * gm.cell_h;
            const bool beyond = (s >= num_steps);

            const float pad = 2.0f;
            const float ix = cx + pad;
            const float iy = cy + pad;
            const float iw = std::max(0.0f, gm.cell_w - 2.0f * pad);
            const float ih = std::max(0.0f, gm.cell_h - 2.0f * pad);
            if (iw <= 0.0f || ih <= 0.0f) continue;

            // Cell background.
            const VividColor cell_bg{0.12f, 0.13f, 0.15f, beyond ? 0.35f : 0.9f};
            vivid::draw_ui::draw_panel(d, o, ix, iy, iw, ih, cell_bg,
                {0, 0, 0, 0}, 2.0f);

            // Active-pattern trigger fill (alpha ∝ velocity).
            const int trig_idx = de::param_index_for(trigger_lane, drum, s);
            const bool trig_active = get_param(trig_idx, 0.0f) > 0.5f;
            const float vel = std::clamp(get_param(
                de::param_index_for(de::LaneKind::ModA, drum, s), 0.5f),
                0.0f, 1.0f);
            if (trig_active && !beyond) {
                const VividColor fill{
                    ed::kDrumColors[drum][0], ed::kDrumColors[drum][1],
                    ed::kDrumColors[drum][2], 0.25f + 0.65f * vel};
                vivid::draw_ui::draw_panel(d, o, ix, iy, iw, ih, fill,
                    {0, 0, 0, 0}, 2.0f);
            } else if (!trig_active && !beyond) {
                // Dim shadow when ANY other pattern fires this cell — quick
                // visual hint that something lives on an inactive bank. With
                // four patterns we union the indication into a single tick
                // so the cell stays readable.
                bool any_other_on = false;
                for (int p = 0; p < static_cast<int>(layout::kPatternCount); ++p) {
                    if (p == active_ptn) continue;
                    if (get_param(de::param_index_for(de::lane_for_pattern(p),
                                                      drum, s), 0.0f) > 0.5f) {
                        any_other_on = true;
                        break;
                    }
                }
                if (any_other_on && d.draw_rect) {
                    d.draw_rect(o, ix, iy, 2.0f, ih,
                        {ed::kDrumColors[drum][0], ed::kDrumColors[drum][1],
                         ed::kDrumColors[drum][2], 0.45f});
                }
            }

            // Probability meter (bottom 3 px strip).
            const float prob = std::clamp(get_param(
                de::param_index_for(de::LaneKind::Probability, drum, s), 1.0f),
                0.0f, 1.0f);
            if (!beyond && prob < 0.999f) {
                const float bar_h = 3.0f;
                const float bar_y = iy + ih - bar_h - 1.0f;
                vivid::draw_ui::draw_meter(d, o, ix + 2.0f, bar_y,
                    std::max(0.0f, iw - 4.0f), bar_h,
                    prob,
                    {0.9f, 0.7f, 0.2f, 0.9f},
                    {0.15f, 0.15f, 0.17f, 0.9f},
                    vivid::draw_ui::MeterOrientation::Horizontal, 1.0f);
            }

            // Roll digit (top-right corner).
            const int roll_count = static_cast<int>(get_param(
                de::param_index_for(de::LaneKind::Roll, drum, s), 1.0f) + 0.5f);
            if (roll_count > 1 && !beyond && d.draw_text) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "%d", roll_count);
                d.draw_text(o, ix + iw - 9.0f, iy + 1.0f, buf,
                    {th.bright_text.r, th.bright_text.g, th.bright_text.b, 0.95f},
                    0.8f);
            }
        }
    }

    // --- Selection + cursor overlays ---
    if (gm.cell_w > 0.0f && gm.cell_h > 0.0f) {
        // Selection rectangle (outline).
        const auto& sel = editor_selection_;
        const float sx = gm.cells_x + sel.col_lo * gm.cell_w;
        const float sy = gm.cells_y + sel.row_lo * gm.cell_h;
        const float sw = (sel.col_hi - sel.col_lo + 1) * gm.cell_w;
        const float sh = (sel.row_hi - sel.row_lo + 1) * gm.cell_h;
        vivid::draw_ui::draw_panel(d, o, sx, sy, sw, sh,
            {0, 0, 0, 0},
            {th.accent.r, th.accent.g, th.accent.b, 0.9f},
            0.0f, 1.0f);

        // Cursor outline (brighter).
        const float cx = gm.cells_x + editor_cursor_step_ * gm.cell_w;
        const float cy = gm.cells_y + editor_cursor_drum_ * gm.cell_h;
        vivid::draw_ui::draw_panel(d, o, cx, cy, gm.cell_w, gm.cell_h,
            {0, 0, 0, 0},
            {th.bright_text.r, th.bright_text.g, th.bright_text.b, 1.0f},
            0.0f, 1.5f);
    }

    // ------------------------------------------------------------
    // Side panel — widget-driven (Phase A of the editor-UI platform).
    //
    // Each widget owns its own hit-test + drag continuation. Callers only
    // inspect the result struct and emit set_param when `clicked` /
    // `changed` fires. SliderState structs live on the core and persist
    // across frames for drag semantics.
    // ------------------------------------------------------------
    constexpr float kSpPad = 10.0f;
    vivid::draw_ui::draw_panel(d, o, side_x, side_y, side_w, side_h,
        {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 0.85f},
        {th.separator.r, th.separator.g, th.separator.b, 0.8f}, 4.0f, 1.0f);

    // Header: span summary. Phase D wires the selection string through
    // ctx.host.set_status_text so the host can render it in the window
    // chrome; we still draw a copy at the top of the side panel for
    // in-editor legibility.
    {
        const int cell_count = de::selection_cell_count(editor_selection_);
        char hdr[96];
        if (cell_count == 1) {
            std::snprintf(hdr, sizeof(hdr), "%s · step %d",
                layout::kDrumLabels[editor_selection_.row_lo],
                editor_selection_.col_lo + 1);
        } else {
            std::snprintf(hdr, sizeof(hdr), "%s-%s · steps %d-%d  (%d cells)",
                layout::kDrumLabels[editor_selection_.row_lo],
                layout::kDrumLabels[editor_selection_.row_hi],
                editor_selection_.col_lo + 1,
                editor_selection_.col_hi + 1,
                cell_count);
        }
        if (d.draw_text) {
            d.draw_text(o, side_x + kSpPad, side_y + kSpPad, hdr,
                {th.bright_text.r, th.bright_text.g, th.bright_text.b, 0.95f},
                1.0f);
        }
        if (ctx->host.set_status_text)
            ctx->host.set_status_text(ctx->host.opaque, hdr);
    }

    // Cursor cell values — widgets render at these values and emit new
    // ones when the user interacts.
    const std::size_t cur_drum = static_cast<std::size_t>(editor_cursor_drum_);
    const int         cur_step = editor_cursor_step_;
    std::array<bool, layout::kPatternCount> cur_trig{};
    for (int p = 0; p < static_cast<int>(layout::kPatternCount); ++p) {
        cur_trig[p] = get_param(
            de::param_index_for(de::lane_for_pattern(p), cur_drum, cur_step),
            0.0f) > 0.5f;
    }
    const float cur_vel = get_param(
        de::param_index_for(de::LaneKind::ModA, cur_drum, cur_step), 0.5f);
    const float cur_modb = get_param(
        de::param_index_for(de::LaneKind::ModB, cur_drum, cur_step), 0.5f);
    const float cur_prob = get_param(
        de::param_index_for(de::LaneKind::Probability, cur_drum, cur_step), 1.0f);
    const int cur_roll = std::clamp(static_cast<int>(get_param(
        de::param_index_for(de::LaneKind::Roll, cur_drum, cur_step), 1.0f) + 0.5f),
        1, 4);

    // Layout cursor carves the padded interior into rows. Header already
    // rendered at y0 + pad, so we reserve ~20px at the top before the
    // first widget row.
    auto sp_cur = vivid::ui::ui_layout(
        vivid::ui::Rect{side_x, side_y, side_w, side_h}, kSpPad, ed::kSideRowGap);
    vivid::ui::ui_row(sp_cur, 20.0f);  // header spacer

    // Row: Trig A / B / C / D toggles. One row, four equally-sized buttons,
    // 4 px gutter between each. Each button toggles its pattern's trigger
    // value across the entire selection.
    {
        auto row = vivid::ui::ui_row(sp_cur, 26.0f);
        const VividColor fill_off{0.20f, 0.20f, 0.23f, 1.0f};
        constexpr VividColor kFillOn[layout::kPatternCount] = {
            {0.70f, 0.45f, 0.25f, 1.0f},  // A
            {0.35f, 0.45f, 0.75f, 1.0f},  // B
            {0.55f, 0.30f, 0.65f, 1.0f},  // C
            {0.30f, 0.65f, 0.45f, 1.0f},  // D
        };
        constexpr const char* kBtnLabels[layout::kPatternCount] = {
            "Trig A", "Trig B", "Trig C", "Trig D"};
        const float gutter = 4.0f;
        const float total_gutter = gutter * (layout::kPatternCount - 1);
        const float btn_w = (row.w - total_gutter) /
                            static_cast<float>(layout::kPatternCount);
        for (int p = 0; p < static_cast<int>(layout::kPatternCount); ++p) {
            const float bx = row.x + p * (btn_w + gutter);
            const vivid::ui::Rect r{bx, row.y, btn_w, row.h};
            auto t = vivid::ui::ui_toggle(*ctx, r, kBtnLabels[p],
                                          cur_trig[p], fill_off, kFillOn[p]);
            if (t.clicked) {
                const de::LaneKind lane = de::lane_for_pattern(p);
                for_each_selected([&](std::size_t dd, int ss) {
                    set_lane(lane, dd, ss, t.value ? 1.0f : 0.0f);
                });
            }
        }
    }

    // Three horizontal sliders: Vel / ModB / Prob. Widget owns drag state
    // via the three SliderState members on the core.
    {
        auto row = vivid::ui::ui_row(sp_cur, 22.0f);
        auto r = vivid::ui::ui_slider_h(*ctx, row, "Vel",
            cur_vel, 0.0f, 1.0f, &sp_vel_drag_);
        if (r.changed) set_velocity_selection(r.value);
    }
    {
        auto row = vivid::ui::ui_row(sp_cur, 22.0f);
        auto r = vivid::ui::ui_slider_h(*ctx, row, "ModB",
            cur_modb, 0.0f, 1.0f, &sp_modb_drag_);
        if (r.changed) set_mod_b_selection(r.value);
    }
    {
        auto row = vivid::ui::ui_row(sp_cur, 22.0f);
        auto r = vivid::ui::ui_slider_h(*ctx, row, "Prob",
            cur_prob, 0.0f, 1.0f, &sp_prob_drag_);
        if (r.changed) set_probability_selection(r.value);
    }

    // Row: roll 1x/2x/3x/4x radio.
    {
        auto row = vivid::ui::ui_row(sp_cur, 26.0f);
        static constexpr const char* kRollLabels[4] = {"1x", "2x", "3x", "4x"};
        auto r = vivid::ui::ui_radio(*ctx, row, kRollLabels, 4, cur_roll - 1);
        if (r.clicked) set_roll_selection(r.value + 1);
    }

    // Probability-prefix mode gets a hint strip at the bottom of the panel.
    if (editor_prob_prefix_mode_ && d.draw_text) {
        d.draw_text(o, side_x + kSpPad, side_y + side_h - 18.0f,
            "Probability: press digit 0-9 (Esc to cancel)",
            {th.accent.r, th.accent.g, th.accent.b, 1.0f}, 0.85f);
    }

    // ------------------------------------------------------------
    // Grid interaction — Phase B of the editor-UI platform plan:
    // ui_step_grid handles click / shift-click / drag / shift-drag.
    // Widgets above have already processed top-bar + side-panel input.
    // ------------------------------------------------------------
    {
        const vivid::ui::Rect grid_bounds{
            gm.cells_x, gm.cells_y, gm.grid_w, gm.grid_h};

        // Cursor hint: a hand when the pointer is inside the grid area
        // (Phase D). The host applies it after draw_editor returns.
        if (ctx->host.set_cursor &&
            grid_bounds.contains(ctx->mouse.x, ctx->mouse.y)) {
            ctx->host.set_cursor(ctx->host.opaque, VIVID_CURSOR_HAND);
        }

        const auto gr = vivid::ui::ui_step_grid(
            *ctx, grid_bounds,
            /*rows=*/static_cast<int>(layout::kDrumCount),
            /*cols=*/static_cast<int>(layout::kStepCount),
            /*active_cols=*/num_steps,
            &grid_state_);

        if (gr.cell_clicked) {
            editor_cursor_drum_ = gr.clicked_row;
            editor_cursor_step_ = gr.clicked_col;
            if (gr.clicked_with_shift) {
                rebuild_selection();
            } else {
                // Plain click: widget already set anchor == clicked cell.
                // Rebuild the drum-sequencer Selection rect (point).
                rebuild_selection();
            }
        }

        if (gr.drag_painting && gr.drag_row >= 0 && gr.drag_col >= 0) {
            // Velocity paint: top of cell = 1.0, bottom = 0.0. Inner
            // padding (2 px top/bottom, matches the glyph inset) makes
            // the mid-cell sit at ~0.5 on click.
            const float inner_frac = std::clamp(
                (gr.drag_mouse_y_in_cell * gm.cell_h - 2.0f) /
                    std::max(1.0f, gm.cell_h - 4.0f),
                0.0f, 1.0f);
            const float vel = 1.0f - inner_frac;
            set_lane(de::LaneKind::ModA,
                static_cast<std::size_t>(gr.drag_row), gr.drag_col, vel);
        }

        if (gr.shift_extending && gr.tip_row >= 0 && gr.tip_col >= 0) {
            editor_cursor_drum_ = gr.tip_row;
            editor_cursor_step_ = gr.tip_col;
            rebuild_selection();
        }
    }
}
