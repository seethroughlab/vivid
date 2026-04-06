#include "clock_core.h"
#include "operator_api/thumbnail.h"
#include "operator_api/draw_plot_helpers.h"

#include <algorithm>
#include <cstdio>

void ClockCore::draw_thumbnail(const VividThumbnailContext* ctx) {
    if (!ctx || !ctx->draw.opaque) return;
    auto& d = const_cast<VividDrawAPI&>(ctx->draw);
    void* o = d.opaque;

    float w = static_cast<float>(ctx->thumbnail_logical_width ? ctx->thumbnail_logical_width : ctx->thumbnail_width);
    float h = static_cast<float>(ctx->thumbnail_logical_height ? ctx->thumbnail_logical_height : ctx->thumbnail_height);
    float beat_phase = (ctx->output_count > 0) ? std::clamp(ctx->output_values[0], 0.0f, 1.0f) : 0.0f;
    float bar_phase = (ctx->output_count > 2) ? std::clamp(ctx->output_values[2], 0.0f, 1.0f) : beat_phase;
    int beats = (ctx->param_count > 1) ? std::clamp(static_cast<int>(ctx->param_values[1]), 1, 16) : 4;
    int rate_mode = (ctx->param_count > 2) ? static_cast<int>(ctx->param_values[2]) : 0;
    int current_beat = std::clamp(static_cast<int>(bar_phase * static_cast<float>(beats)), 0, beats - 1);

    vivid::draw_plot::draw_thumb_background(d, o, w, h);

    char bpm_label[16];
    float bpm_value = (ctx->param_count > 0) ? ctx->param_values[0] : 120.0f;
    if (rate_mode == 1) {
        std::snprintf(bpm_label, sizeof(bpm_label), "SYNC");
    } else {
        std::snprintf(bpm_label, sizeof(bpm_label), "%.0f BPM", bpm_value);
    }
    vivid::draw_plot::draw_thumb_label(d, o, 6.0f, 4.0f, bpm_label, {0.45f, 0.55f, 0.65f, 0.9f}, 0.8f);

    float play_x = 8.0f;
    float play_y = 22.0f;
    float play_w = w - 16.0f;
    float play_h = 18.0f;
    vivid::draw_ui::draw_panel(d, o, play_x, play_y, play_w, play_h,
                               {0.14f, 0.16f, 0.19f, 0.7f}, {0, 0, 0, 0}, 3.0f);
    vivid::draw_plot::draw_playhead_line(d, o,
                                         play_x + beat_phase * play_w, play_y,
                                         play_x + beat_phase * play_w, play_y + play_h,
                                         {1.0f, 0.78f, 0.31f, 0.9f}, 2.0f);

    int active[16] = {};
    for (int i = 0; i < beats && i < 16; ++i) active[i] = 1;
    vivid::draw_plot::draw_step_grid(d, o, 8.0f, 48.0f, w - 16.0f, h - 56.0f,
                                     beats, active, current_beat,
                                     {0.36f, 0.55f, 0.75f, 0.7f},
                                     {0.18f, 0.2f, 0.24f, 0.45f},
                                     {0.31f, 0.51f, 0.86f, 0.35f},
                                     beats <= 8 ? 3.0f : 2.0f,
                                     2.0f);
}

// Thumbnail entry point is exported by each _fr/_au wrapper via VIVID_THUMBNAIL.
