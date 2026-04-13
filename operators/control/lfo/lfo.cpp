#include "lfo.h"
#include "operator_api/thumbnail.h"
#include "operator_api/draw_plot_helpers.h"

#include <algorithm>
#include <cmath>

namespace {

float thumb_hash(int i, int seed) {
    uint32_t x = static_cast<uint32_t>(i * 1664525 + seed * 1013904223);
    x ^= x >> 16;
    x *= 2246822519u;
    x ^= x >> 13;
    return static_cast<float>(x & 0xffffu) / 65535.0f;
}

float thumb_lfo_value(float phase, int waveform, int distribution) {
    float p = phase - std::floor(phase);
    switch (waveform) {
        case 0: return std::sin(p * 2.0f * static_cast<float>(M_PI));
        case 1: return 2.0f * p - 1.0f;
        case 2: return (p < 0.5f) ? 1.0f : -1.0f;
        case 3: return 4.0f * ((p < 0.5f) ? p : 1.0f - p) - 1.0f;
        case 4: {
            int step = static_cast<int>(std::floor(p * 8.0f));
            float v = thumb_hash(step, 17 + distribution * 11) * 2.0f - 1.0f;
            return v;
        }
        case 5: {
            float fp = p * 6.0f;
            int step = static_cast<int>(std::floor(fp));
            float t = fp - static_cast<float>(step);
            float a = thumb_hash(step, 29 + distribution * 7) * 2.0f - 1.0f;
            float b = thumb_hash(step + 1, 29 + distribution * 7) * 2.0f - 1.0f;
            return a + (b - a) * (t * t * (3.0f - 2.0f * t));
        }
        case 6:
        default:
            return thumb_hash(static_cast<int>(p * 24.0f), 47 + distribution * 13) * 2.0f - 1.0f;
    }
}

const char* thumb_wave_name(int waveform) {
    switch (waveform) {
        case 0: return "SIN";
        case 1: return "SAW";
        case 2: return "SQR";
        case 3: return "TRI";
        case 4: return "S&H";
        case 5: return "SMTH";
        case 6: return "NOISE";
        default: return "LFO";
    }
}

} // namespace

void LFO::draw_thumbnail(const VividThumbnailContext* ctx) {
    if (!ctx || !ctx->draw.opaque) return;
    auto& d = const_cast<VividDrawAPI&>(ctx->draw);
    void* o = d.opaque;

    float w = static_cast<float>(ctx->thumbnail_logical_width ? ctx->thumbnail_logical_width : ctx->thumbnail_width);
    float h = static_cast<float>(ctx->thumbnail_logical_height ? ctx->thumbnail_logical_height : ctx->thumbnail_height);
    int waveform = (ctx->param_count > 5) ? static_cast<int>(ctx->param_values[5]) : 0;
    int polarity = (ctx->param_count > 7) ? static_cast<int>(ctx->param_values[7]) : 0;
    int distribution = (ctx->param_count > 9) ? static_cast<int>(ctx->param_values[9]) : 0;
    float phase = (ctx->output_count > 1) ? ctx->output_values[1] : 0.0f;
    bool unipolar = polarity > 0;

    vivid::draw_plot::draw_thumb_background(d, o, w, h);
    vivid::draw_plot::draw_thumb_label(d, o, 6.0f, 4.0f, thumb_wave_name(waveform), {0.45f, 0.55f, 0.65f, 0.9f}, 0.8f);
    if (unipolar) {
        vivid::draw_plot::draw_thumb_value(d, o, w - 34.0f, 4.0f, 28.0f, "UNI", {0.7f, 0.55f, 0.35f, 0.95f}, 0.75f);
    }

    auto sample_fn = [waveform, distribution, unipolar](float phase) {
        float raw = thumb_lfo_value(phase, waveform, distribution);
        return unipolar ? (raw * 0.5f + 0.5f) * 2.0f - 1.0f : raw;
    };

    // Plot area geometry
    constexpr float plot_x = 8.0f;
    constexpr float plot_top = 22.0f;
    constexpr float pad = 2.0f;
    float plot_w = w - 16.0f;
    float plot_h = h - 30.0f;

    vivid::draw_plot::draw_waveform_plot(d, o,
                                         plot_x, plot_top, plot_w, plot_h,
                                         sample_fn,
                                         {0.31f, 0.51f, 0.75f, 0.35f},
                                         {0.63f, 0.78f, 0.94f, 0.95f},
                                         {0.24f, 0.25f, 0.29f, 0.7f},
                                         !unipolar,
                                         1.0f,
                                         pad);

    // Vertical playhead at current phase + dot at waveform intersection
    float p = phase - std::floor(phase);
    float cursor_x = plot_x + pad + p * std::max(1.0f, plot_w - 2.0f * pad);
    float inner_top = plot_top + pad;
    float inner_h = std::max(1.0f, plot_h - 2.0f * pad);
    float center_y = inner_top + inner_h * 0.5f;

    // Vertical cursor line
    VividColor cursor_color = {1.0f, 0.78f, 0.31f, 0.45f};
    vivid::draw_plot::draw_playhead_line(d, o, cursor_x, inner_top, cursor_x, inner_top + inner_h,
                                         cursor_color, 1.0f);

    // Dot at the waveform intersection
    float sample = std::clamp(sample_fn(p), -1.0f, 1.0f);
    float dot_y = center_y - sample * (inner_h * 0.45f);
    constexpr float dot_r = 3.0f;
    VividColor dot_color = {1.0f, 0.78f, 0.31f, 0.95f};
    if (d.draw_rounded_rect) {
        d.draw_rounded_rect(o, cursor_x - dot_r, dot_y - dot_r,
                            dot_r * 2.0f, dot_r * 2.0f, dot_r, dot_color);
    } else if (d.draw_rect) {
        d.draw_rect(o, cursor_x - dot_r, dot_y - dot_r,
                    dot_r * 2.0f, dot_r * 2.0f, dot_color);
    }
}
