#include "sample_hold.h"
#include "operator_api/thumbnail.h"
#include <cstdio>

// Simple deterministic hash for decorative staircase pattern
static uint32_t pcg_hash(uint32_t input) {
    uint32_t state = input * 747796405u + 2891336453u;
    uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

void SampleHold::draw_thumbnail(const VividThumbnailContext* ctx) {
    if (!ctx || !ctx->draw.opaque) return;
    const auto& d = ctx->draw;
    void* o = d.opaque;

    float w = static_cast<float>(ctx->thumbnail_logical_width ? ctx->thumbnail_logical_width : ctx->thumbnail_width);
    float h = static_cast<float>(ctx->thumbnail_logical_height ? ctx->thumbnail_logical_height : ctx->thumbnail_height);

    float held = (ctx->output_count > 0) ? ctx->output_values[0] : 0.0f;
    int m = (ctx->param_count > 0) ? static_cast<int>(ctx->param_values[0]) : 0;

    // Dark background
    d.draw_rect(o, 0, 0, w, h, {0.07f, 0.08f, 0.09f, 0.9f});

    // Mode label
    const char* mode_label = (m == 0) ? "S&H" : "T&H";
    d.draw_text(o, 5, 3, mode_label, {0.45f, 0.55f, 0.65f, 0.8f}, 0.85f);

    // Held value text
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.2f", held);
    float tw = d.text_width(o, buf, 0.75f);
    d.draw_text(o, w - tw - 5, 3, buf, {1.0f, 0.78f, 0.31f, 0.8f}, 0.75f);

    // Staircase visualization
    float margin = 4.0f;
    float top_y = 18.0f;
    float plot_h = h - top_y - margin;
    float plot_w = w - 2 * margin;

    VividColor step_col = {0.31f, 0.51f, 0.75f, 0.5f};
    VividColor line_col = {0.63f, 0.78f, 0.94f, 0.8f};
    VividColor held_col = {1.0f, 0.78f, 0.31f, 0.85f};

    // Draw 6 decorative steps
    constexpr int kSteps = 6;
    float step_w = plot_w / kSteps;
    float prev_level = 0.5f;

    for (int i = 0; i < kSteps; ++i) {
        float rnd = static_cast<float>(pcg_hash(static_cast<uint32_t>(i) + 42u) % 1000u) / 1000.0f;
        float level = rnd * 0.7f + 0.15f;  // 0.15 to 0.85
        float sy = top_y + plot_h * (1.0f - level);
        float sx = margin + i * step_w;
        float fill_h = plot_h * level;

        // Step fill
        d.draw_rect(o, sx, sy, step_w - 1, fill_h, step_col);

        // Step top line
        d.draw_line(o, sx, sy, sx + step_w - 1, sy, 1.5f, line_col);

        // Vertical transition from previous step
        if (i > 0) {
            float prev_y = top_y + plot_h * (1.0f - prev_level);
            float lo = std::min(sy, prev_y);
            float hi = std::max(sy, prev_y);
            d.draw_line(o, sx, lo, sx, hi, 1.5f, line_col);
        }

        prev_level = level;
    }

    // Held value indicator line (horizontal across full width)
    float held_clamped = std::clamp(held, 0.0f, 1.0f);
    float held_y = top_y + plot_h * (1.0f - (held_clamped * 0.7f + 0.15f));
    d.draw_line(o, margin, held_y, margin + plot_w, held_y, 2.0f, held_col);
}

// --- Audio-rate operator ---

#include "control/audio_scalar_utils.h"

struct SampleHoldAudio : SampleHold, vivid::AudioProcessable {
    static constexpr const char* kName = "SampleHold";

    void process_audio(const VividAudioContext* ctx) override {
        int m = mode.int_value();

        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            float signal = vivid::audio_scalar_sample(ctx, 0, i);
            bool trig = vivid::audio_scalar_sample(ctx, 1, i) > 0.5f;
            advance(signal, trig, m);
            ctx->output_buffers[0][i] = held_value_;
        }
    }
};

VIVID_DEFINE_OP(SampleHoldAudio) {
}

VIVID_THUMBNAIL(SampleHoldAudio)
