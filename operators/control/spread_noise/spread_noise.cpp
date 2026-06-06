#include "operator_api/operator.h"
#include "operator_api/thumbnail.h"
#include "operator_api/draw_plot_helpers.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {

inline float spread_hash01(uint32_t input) {
    uint32_t state  = input * 747796405u + 2891336453u;
    uint32_t word   = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    uint32_t result = (word >> 22u) ^ word;
    return static_cast<float>(result) / 4294967295.0f;
}

inline float spread_noise01(uint32_t i, float t, uint32_t seed) {
    float ti       = t + static_cast<float>(i) * 0.618033988749895f;
    float ti_floor = std::floor(ti);
    float frac     = ti - ti_floor;
    int32_t t0     = static_cast<int32_t>(ti_floor);
    int32_t t1     = t0 + 1;
    uint32_t key0  = static_cast<uint32_t>(t0 + static_cast<int32_t>(i * 7919 + seed));
    uint32_t key1  = static_cast<uint32_t>(t1 + static_cast<int32_t>(i * 7919 + seed));
    float v0       = spread_hash01(key0);
    float v1       = spread_hash01(key1);
    float smooth   = frac * frac * (3.0f - 2.0f * frac);
    return v0 + (v1 - v0) * smooth;
}

} // namespace

/**
 * @brief Emit N independently-animated hash-based 1D value noise lanes.
 *
 * Produces a lane array where each lane evolves as smooth value noise,
 * decorrelated across lanes via golden-ratio sample offsets. Useful as a
 * per-instance driver for Instancer3D / Instancer2D position, scale,
 * rotation, or color, or as a polyphonic jitter source.
 *
 * @param count     Number of noise lanes to emit (1–1024).
 * @param speed     Animation rate — how fast each lane's noise evolves.
 * @param amplitude Output range is [offset, offset + amplitude].
 * @param offset    Additive bias applied to every lane.
 * @param seed      Decorrelation seed.
 */
struct SpreadNoise : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "SpreadNoise";
    static constexpr bool kTimeDependent = true;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_STRUCTURAL;

    vivid::Param<int>   count     {"count",     125, 1, 1024};
    vivid::Param<float> speed     {"speed",     1.0f, 0.0f, 20.0f};
    vivid::Param<float> amplitude {"amplitude", 1.0f, 0.0f, 100.0f};
    vivid::Param<float> offset    {"offset",    0.0f, -100.0f, 100.0f};
    vivid::Param<int>   seed      {"seed",      42, 0, 99999};

    SpreadNoise() {
        vivid::semantic_tag(count, "count");
        vivid::semantic_shape(count, "int");

        vivid::semantic_tag(amplitude, "amplitude_linear");
        vivid::semantic_shape(amplitude, "scalar");

        vivid::semantic_tag(seed, "seed");
        vivid::semantic_shape(seed, "int");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&count);
        out.push_back(&speed);
        out.push_back(&amplitude);
        out.push_back(&offset);
        out.push_back(&seed);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"values", VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT,
                       VIVID_PORT_TRANSPORT_LANE_ARRAY, 0, nullptr, 0, 0.0f,
                       nullptr, nullptr, nullptr, nullptr, nullptr});
    }

    void process_frame(const VividFrameContext* ctx) override {
        if (!ctx->value_outputs) return;

        float dt  = static_cast<float>(ctx->delta_time);
        float spd = ctx->param_values[1];
        float amp = ctx->param_values[2];
        float off = ctx->param_values[3];
        uint32_t s = static_cast<uint32_t>(ctx->param_values[4]);

        time_ += dt * spd;

        uint32_t n = std::clamp(static_cast<uint32_t>(ctx->param_values[0]), 1u, 1024u);

        float* buf = vivid_value_output_floats(&ctx->value_outputs[0], n);
        if (!buf) return;

        for (uint32_t i = 0; i < n; ++i) {
            buf[i] = spread_noise01(i, time_, s) * amp + off;
        }

        vivid_value_output_commit(&ctx->value_outputs[0], n);
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        if (!ctx || !ctx->draw.opaque) return;
        auto& d = const_cast<VividDrawAPI&>(ctx->draw);
        void* o = d.opaque;

        float w = static_cast<float>(ctx->thumbnail_logical_width
                  ? ctx->thumbnail_logical_width  : ctx->thumbnail_width);
        float h = static_cast<float>(ctx->thumbnail_logical_height
                  ? ctx->thumbnail_logical_height : ctx->thumbnail_height);

        int      count_p = (ctx->param_count > 0) ? static_cast<int>(ctx->param_values[0]) : 125;
        float    speed_p = (ctx->param_count > 1) ? ctx->param_values[1] : 1.0f;
        uint32_t seed_p  = (ctx->param_count > 4) ? static_cast<uint32_t>(ctx->param_values[4]) : 42u;

        int   n = std::clamp(count_p, 1, 1024);
        float t = static_cast<float>(ctx->time) * std::max(0.0f, speed_p);

        vivid::draw_plot::draw_thumb_background(d, o, w, h);
        vivid::draw_plot::draw_thumb_label(d, o, 6.0f, 4.0f, "SPREAD",
                                           {0.45f, 0.55f, 0.65f, 0.9f}, 0.8f);

        char count_buf[16];
        std::snprintf(count_buf, sizeof(count_buf), "N=%d", n);
        vivid::draw_plot::draw_thumb_value(d, o, w - 38.0f, 4.0f, 32.0f, count_buf,
                                           {0.70f, 0.55f, 0.35f, 0.85f}, 0.75f);

        float plot_y   = 18.0f;
        float plot_h   = h - plot_y - 6.0f;
        float plot_x   = 6.0f;
        float plot_w   = w - 12.0f;
        float center_y = plot_y + plot_h * 0.5f;

        if (d.draw_line) {
            d.draw_line(o, plot_x, center_y, plot_x + plot_w, center_y,
                        1.0f, {0.24f, 0.25f, 0.29f, 0.55f});
        }

        constexpr int   kPlotDots = 64;
        constexpr float kDotR     = 1.25f;
        int   dots        = std::min(n, kPlotDots);
        float lane_stride = static_cast<float>(n) / static_cast<float>(dots);

        for (int j = 0; j < dots; ++j) {
            uint32_t lane = static_cast<uint32_t>(j * lane_stride);
            float v01 = spread_noise01(lane, t, seed_p);
            float vbi = v01 * 2.0f - 1.0f;
            float dx  = plot_x + (static_cast<float>(j) + 0.5f) * (plot_w / static_cast<float>(dots));
            float dy  = center_y - vbi * (plot_h * 0.45f);

            float intensity = std::min(1.0f, std::fabs(vbi) * 1.2f + 0.35f);
            VividColor c{0.55f + 0.30f * intensity,
                         0.70f + 0.20f * intensity,
                         0.90f,
                         0.55f + 0.40f * intensity};

            if (d.draw_rounded_rect) {
                d.draw_rounded_rect(o, dx - kDotR, dy - kDotR,
                                    kDotR * 2.0f, kDotR * 2.0f, kDotR, c);
            } else if (d.draw_rect) {
                d.draw_rect(o, dx - kDotR, dy - kDotR,
                            kDotR * 2.0f, kDotR * 2.0f, c);
            }
        }
    }

private:
    float time_ = 0.0f;
};

VIVID_DEFINE_OP(SpreadNoise) {
}

VIVID_THUMBNAIL(SpreadNoise)
