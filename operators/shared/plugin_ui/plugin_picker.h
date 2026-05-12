#pragma once
#include "operator_api/types.h"
#include "operator_api/draw_ui_helpers.h"
#include <vector>
#include <string>

// Shared plugin picker widget for CLAP, AU, and VST3 inspector panels.
// Renders a collapsible list of plugin names inside a VIVID_INSPECTOR section.
//
// Usage:
//   int sel = vivid::plugin_ui::draw_plugin_picker(ctx, y, names, current_idx, state);
//   if (sel >= 0) { /* plugin at index sel was selected */ }

namespace vivid::plugin_ui {

struct PluginPickerState {
    bool open        = false;
    int  hovered_idx = -1;
};

static constexpr float kPickerRowH  = 28.f;  // header row height
static constexpr float kPickerItemH = 20.f;  // per-item row height
static constexpr float kPickerPad   =  8.f;  // left indent for text

// Draw a collapsible plugin picker. Returns the newly-selected index (0-based)
// or -1 if no change this frame. `y` is advanced by the widget's total height.
inline int draw_plugin_picker(
    VividInspectorContext*      ctx,
    float&                      y,
    const std::vector<std::string>& names,
    int                         current_idx,
    PluginPickerState&          state)
{
    auto& d        = ctx->draw;
    void* o        = d.opaque;
    const auto& th = ctx->theme;
    const auto& m  = ctx->mouse;

    const float x = ctx->content_x;
    const float w = ctx->content_width;
    int changed_to = -1;

    // ---- Header row ----
    bool hdr_hit = m.x >= x && m.x <= x + w && m.y >= y && m.y <= y + kPickerRowH;
    VividColor hdr_bg = hdr_hit
        ? VividColor{th.dark_bg.r + 0.06f, th.dark_bg.g + 0.06f, th.dark_bg.b + 0.06f, 1.f}
        : th.dark_bg;
    if (d.draw_rounded_rect)
        d.draw_rounded_rect(o, x, y, w, kPickerRowH, th.corner_radius, hdr_bg);

    // Arrow indicator
    const char* arrow = state.open ? "v " : "> ";
    float lh = vivid::draw_ui::line_height_or(d, o, 13.f);
    float ty  = y + (kPickerRowH - lh) * 0.5f;
    if (d.draw_text) d.draw_text(o, x + kPickerPad, ty, arrow, th.dim_text, 1.f);

    // Current plugin name (or placeholder)
    const char* label = (current_idx >= 0 && current_idx < (int)names.size())
        ? names[current_idx].c_str() : "(none)";
    VividColor label_col = (current_idx >= 0) ? th.bright_text : th.dim_text;
    if (d.draw_text) d.draw_text(o, x + kPickerPad + 18.f, ty, label, label_col, 1.f);

    if (m.left_clicked && hdr_hit)
        state.open = !state.open;

    y += kPickerRowH + 2.f;

    // ---- Expanded list ----
    if (state.open) {
        if (names.empty()) {
            if (d.draw_text)
                d.draw_text(o, x + kPickerPad + 12.f, y + 3.f,
                            "(no plugins found)", th.dim_text, 0.9f);
            y += kPickerItemH;
        } else {
            for (int i = 0; i < (int)names.size(); ++i) {
                bool is_sel  = (i == current_idx);
                bool is_hov  = m.x >= x && m.x <= x + w
                            && m.y >= y && m.y <= y + kPickerItemH;

                if (is_sel) {
                    VividColor sel_bg = {th.accent.r, th.accent.g, th.accent.b, 0.20f};
                    if (d.draw_rect) d.draw_rect(o, x, y, w, kPickerItemH, sel_bg);
                } else if (is_hov) {
                    VividColor hov_bg = {th.bright_text.r, th.bright_text.g,
                                         th.bright_text.b, 0.07f};
                    if (d.draw_rect) d.draw_rect(o, x, y, w, kPickerItemH, hov_bg);
                }

                // Bullet for selected item
                if (is_sel && d.draw_text)
                    d.draw_text(o, x + kPickerPad, y + 3.f, "*", th.accent, 0.9f);

                VividColor tc = is_sel ? th.bright_text : th.dim_text;
                if (d.draw_text)
                    d.draw_text(o, x + kPickerPad + 12.f, y + 3.f,
                                names[i].c_str(), tc, 0.9f);

                if (m.left_clicked && is_hov && !is_sel) {
                    changed_to = i;
                    state.open = false;
                }

                y += kPickerItemH;
            }
        }
        y += 4.f;  // bottom gap
    }

    return changed_to;
}

// Draw an "Open Plugin UI" button. Returns true if clicked this frame.
// Only drawn when `enabled` is true (i.e. a plugin with GUI is loaded).
inline bool draw_open_gui_button(
    VividInspectorContext* ctx,
    float& y,
    bool   enabled,
    bool   gui_open)
{
    auto& d        = ctx->draw;
    void* o        = d.opaque;
    const auto& th = ctx->theme;
    const auto& m  = ctx->mouse;

    const float x  = ctx->content_x;
    const float w  = ctx->content_width;
    const float bh = 28.f;

    if (!enabled) {
        y += bh + 4.f;
        return false;
    }

    bool hovered = m.x >= x && m.x <= x + w && m.y >= y && m.y <= y + bh;
    VividColor fill        = gui_open
        ? VividColor{0.16f, 0.50f, 0.16f, 1.f}
        : VividColor{0.13f, 0.38f, 0.60f, 1.f};
    VividColor active_fill = gui_open
        ? VividColor{0.20f, 0.65f, 0.20f, 1.f}
        : VividColor{0.18f, 0.50f, 0.78f, 1.f};
    const char* label = gui_open ? "Plugin UI Open" : "Open Plugin UI";

    vivid::draw_ui::draw_button(d, o, x, y, w, bh, label, hovered,
                                fill, active_fill, th.bright_text);
    y += bh + 4.f;

    return m.left_clicked && hovered && !gui_open;
}

} // namespace vivid::plugin_ui
