#include "drum_sequencer_core.h"
#include "operator_api/draw_ui_helpers.h"
#include <algorithm>
#include <string>

namespace drum_insp {
static constexpr float kDrumColors[6][3] = {
    {0.86f, 0.31f, 0.31f}, {0.86f, 0.75f, 0.24f}, {0.24f, 0.78f, 0.71f},
    {0.31f, 0.51f, 0.86f}, {0.63f, 0.35f, 0.78f}, {0.31f, 0.78f, 0.39f},
};
static const char* kTabLabels[] = {"Pattern", "Mod A", "Mod B"};
static constexpr float kLabelW = 28.0f;
static constexpr float kCellH = 14.0f;
static constexpr float kCellPad = 2.0f;
static constexpr float kTabW = 80.0f;
static constexpr float kTabH = 18.0f;
} // namespace drum_insp

void DrumSequencerCore::draw_inspector(VividInspectorContext* ctx) {
    namespace di = drum_insp;
    namespace layout = vivid_sequencers::drum_layout;
    auto& d = ctx->draw;
    void* o = d.opaque;
    const auto& th = ctx->theme;
    const auto& mouse = ctx->mouse;

    float px = ctx->content_x;
    float base_y = ctx->content_y;
    float panel_w = ctx->content_width;

    int num_steps = (ctx->param_count > 0) ? std::clamp(static_cast<int>(ctx->param_values[0]), 1, 16) : 16;

    // Current step from output[6]
    int current_step = -1;
    if (ctx->output_count > layout::kStepOutputIndex)
        current_step = static_cast<int>(ctx->output_values[layout::kStepOutputIndex]);

    // Layout
    float grid_w = panel_w - di::kLabelW;
    float cell_w = grid_w / 16.0f;
    float grid_h = 6.0f * di::kCellH;

    float y = 4.0f; // relative y offset

    vivid::draw_ui::draw_tab_strip(d, o,
                                   px, base_y + y,
                                   di::kTabW, di::kTabH,
                                   di::kTabLabels, 3, insp_tab_,
                                   {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.5f},
                                   {th.dim_text.r * 1.5f, th.dim_text.g * 1.5f, th.dim_text.b * 1.5f, 1.0f},
                                   {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 0.9f},
                                   {th.accent.r, th.accent.g, th.accent.b, 1.0f});
    for (int t = 0; t < 3; ++t) {
        float tx = static_cast<float>(t) * di::kTabW;
        if (mouse.left_clicked &&
            mouse.x >= tx && mouse.x < tx + di::kTabW &&
            mouse.y >= y && mouse.y < y + di::kTabH) {
            insp_tab_ = t;
        }
    }

    y += di::kTabH + 2;

    float total_h = grid_h + 8.0f;

    // Dark background for grid area
    vivid::draw_ui::draw_panel(d, o, px, base_y + y, panel_w, total_h,
                               {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 0.9f});

    float grid_x = di::kLabelW;     // relative x
    float grid_y = y + 4.0f;        // relative y

    // Current step column highlight
    if (current_step >= 0 && current_step < num_steps) {
        vivid::draw_ui::draw_selection_highlight(d, o,
            px + grid_x + current_step * cell_w, base_y + grid_y,
            cell_w, grid_h,
            {th.accent.r, th.accent.g, th.accent.b, 1.0f});
    }

    // Beat group separators
    for (int b = 1; b < 4; ++b) {
        float sx = grid_x + b * 4 * cell_w;
        d.draw_rect(o, px + sx - 0.5f, base_y + grid_y, 1.0f, grid_h,
                    {th.separator.r, th.separator.g, th.separator.b, 0.6f});
    }

    for (std::size_t drum = 0; drum < layout::kDrumCount; ++drum) {
        float row_y = grid_y + drum * di::kCellH;

        // Row label
        d.draw_text(o, px + 2, base_y + row_y + 1, layout::kDrumLabels[drum],
                    {di::kDrumColors[drum][0], di::kDrumColors[drum][1], di::kDrumColors[drum][2], 0.8f}, 1.0f);

        for (std::size_t s = 0; s < layout::kStepCount; ++s) {
            float cx = grid_x + s * cell_w;
            float cy = row_y;
            bool beyond_steps = (s >= num_steps);

            bool trigger_active = false;
            int trig_idx = layout::trigger_param_index(drum, static_cast<int>(s));
            if (trig_idx < static_cast<int>(ctx->param_count))
                trigger_active = ctx->param_values[trig_idx] > 0.5f;

            if (insp_tab_ == 0) {
                // Pattern tab: boolean toggle
                if (trigger_active) {
                    float alpha = beyond_steps ? 0.25f : 0.9f;
                    vivid::draw_ui::draw_grid_cell(d, o,
                        px + cx + di::kCellPad, base_y + cy + di::kCellPad,
                        cell_w - 2 * di::kCellPad, di::kCellH - 2 * di::kCellPad,
                        "",
                        {di::kDrumColors[drum][0], di::kDrumColors[drum][1], di::kDrumColors[drum][2], alpha},
                        {0.0f, 0.0f, 0.0f, 0.0f},
                        1.5f, 1.0f);
                }

                if (mouse.left_clicked &&
                    mouse.x >= cx && mouse.x < cx + cell_w &&
                    mouse.y >= cy && mouse.y < cy + di::kCellH) {
                    std::string name = std::string(layout::kTriggerPrefixes[drum]) + std::to_string(s);
                    ctx->commands.set_param(ctx->commands.opaque, name.c_str(),
                                            trigger_active ? 0.0f : 1.0f);
                }
            } else {
                // Mod A or Mod B tab
                int mod_idx = (insp_tab_ == 1)
                    ? layout::mod_a_param_index(drum, static_cast<int>(s))
                    : layout::mod_b_param_index(drum, static_cast<int>(s));
                float mod_val = 0.5f;
                if (mod_idx < static_cast<int>(ctx->param_count))
                    mod_val = ctx->param_values[mod_idx];

                float base_alpha = beyond_steps ? 0.25f : (trigger_active ? 0.8f : 0.3f);

                // Dark track background
                vivid::draw_ui::draw_panel(d, o,
                            px + cx + di::kCellPad, base_y + cy + di::kCellPad,
                            cell_w - 2 * di::kCellPad, di::kCellH - 2 * di::kCellPad,
                            {0.1f, 0.1f, 0.12f, base_alpha}, {0, 0, 0, 0}, 1.5f);

                // Fill bar from bottom
                float inner_h = di::kCellH - 2 * di::kCellPad;
                float fill_h = mod_val * inner_h;
                d.draw_rect(o, px + cx + di::kCellPad,
                            base_y + cy + di::kCellPad + inner_h - fill_h,
                            cell_w - 2 * di::kCellPad, fill_h,
                            {di::kDrumColors[drum][0], di::kDrumColors[drum][1], di::kDrumColors[drum][2], base_alpha});

                // Click to start drag
                if (mouse.left_clicked &&
                    mouse.x >= cx && mouse.x < cx + cell_w &&
                    mouse.y >= cy && mouse.y < cy + di::kCellH) {
                    insp_dragging_ = true;
                    insp_drag_drum_ = drum;
                    insp_drag_step_ = s;
                }
            }
        }
    }

    // Handle mod drag (continuous while held)
    if (insp_dragging_ && mouse.left_down) {
        const auto& mod_prefix = (insp_tab_ == 1)
            ? layout::kModAPrefixes
            : layout::kModBPrefixes;
        float cell_y = grid_y + insp_drag_drum_ * di::kCellH;
        float inner_y = cell_y + di::kCellPad;
        float inner_h = di::kCellH - 2 * di::kCellPad;
        float t = 1.0f - (mouse.y - inner_y) / inner_h;
        t = std::max(0.0f, std::min(1.0f, t));

        std::string name = std::string(mod_prefix[static_cast<std::size_t>(insp_drag_drum_)])
            + std::to_string(insp_drag_step_);
        ctx->commands.set_param(ctx->commands.opaque, name.c_str(), t);
    }

    if (mouse.left_released) {
        insp_dragging_ = false;
        insp_drag_drum_ = -1;
        insp_drag_step_ = -1;
    }

    y += total_h + 4;
    ctx->consumed_height = y;
}
