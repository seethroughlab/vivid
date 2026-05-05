#include "gate.h"
#include "operator_api/thumbnail.h"

#include <algorithm>
#include <cstdio>

void Gate::draw_thumbnail(const VividThumbnailContext* ctx) {
    if (!ctx || !ctx->draw.opaque) return;
    const auto& d = ctx->draw;
    void* o = d.opaque;

    float thresh = (ctx->param_count > 0) ? std::clamp(ctx->param_values[0], 0.0f, 1.0f) : 0.5f;
    float inv    = (ctx->param_count > 1) ? ctx->param_values[1] : 0.0f;
    float gate_open = (ctx->output_count > 1) ? ctx->output_values[1] : 0.0f;
    bool inverted = inv > 0.5f;
    bool is_open  = gate_open > 0.5f;

    float w = static_cast<float>(ctx->thumbnail_logical_width  ? ctx->thumbnail_logical_width  : ctx->thumbnail_width);
    float h = static_cast<float>(ctx->thumbnail_logical_height ? ctx->thumbnail_logical_height : ctx->thumbnail_height);

    // Background
    d.draw_rect(o, 0, 0, w, h, {0.07f, 0.08f, 0.09f, 0.9f});

    // Layout
    float pad = 6.0f;
    float text_top = 3.0f;
    float plot_top = 22.0f;
    float plot_h = h - plot_top - pad;

    // Status label: OPEN / CLOSED
    VividColor open_col   = {0.31f, 0.75f, 0.47f, 1.0f};
    VividColor closed_col = {0.55f, 0.58f, 0.65f, 0.7f};
    const char* status_text = is_open ? "OPEN" : "CLOSED";
    VividColor status_col   = is_open ? open_col : closed_col;
    d.draw_text(o, pad, text_top, status_text, status_col, 1.0f);

    // INV label (right-aligned)
    if (inverted) {
        float inv_w = d.text_width ? d.text_width(o, "INV", 1.0f) : 18.0f;
        d.draw_text(o, w - pad - inv_w, text_top, "INV", {0.7f, 0.55f, 0.35f, 1.0f}, 1.0f);
    }

    // Threshold label row
    char thresh_label[16];
    std::snprintf(thresh_label, sizeof(thresh_label), "%.0f%%", thresh * 100.0f);
    float tl_w = d.text_width ? d.text_width(o, thresh_label, 0.85f) : 20.0f;
    d.draw_text(o, w - pad - tl_w, text_top + 12.0f, thresh_label, {0.45f, 0.55f, 0.65f, 0.8f}, 0.85f);

    // Threshold line Y position within plot area
    float thresh_y = plot_top + (1.0f - thresh) * plot_h;

    // Fill the active region
    VividColor fill_col = is_open ? VividColor{open_col.r, open_col.g, open_col.b, 0.3f}
                                  : VividColor{0.24f, 0.25f, 0.29f, 0.3f};
    if (!inverted) {
        // Active region is above threshold (lower Y values)
        float region_h = thresh_y - plot_top;
        if (region_h > 0)
            d.draw_rect(o, pad, plot_top, w - 2 * pad, region_h, fill_col);
    } else {
        // Active region is below threshold (higher Y values)
        float region_h = (plot_top + plot_h) - thresh_y;
        if (region_h > 0)
            d.draw_rect(o, pad, thresh_y, w - 2 * pad, region_h, fill_col);
    }

    // Threshold line
    VividColor line_col = {0.78f, 0.82f, 0.86f, 0.85f};
    d.draw_line(o, pad, thresh_y, w - pad, thresh_y, 2.0f, line_col);
}

// --- Audio-rate operator ---

#include "control/audio_scalar_utils.h"

struct GateAudio : Gate, vivid::AudioProcessable {
    static constexpr const char* kName = "Gate";

    void process_audio(const VividAudioContext* ctx) override {
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            float signal = vivid::audio_scalar_sample(ctx, 0, i);
            float gate_in = vivid::audio_scalar_sample(ctx, 1, i);
            bool is_open = gate_in > threshold.value;
            if (invert.bool_value()) is_open = !is_open;

            ctx->output_buffers[0][i] = is_open ? signal : 0.0f;
            ctx->output_buffers[1][i] = is_open ? 1.0f : 0.0f;
        }
    }
};

VIVID_DEFINE_OP(GateAudio) {
}

VIVID_THUMBNAIL(GateAudio)
