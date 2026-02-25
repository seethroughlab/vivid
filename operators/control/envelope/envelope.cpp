#include "operator_api/operator.h"
#include <cmath>
#include <cstring>

struct Envelope : vivid::OperatorBase {
    static constexpr const char* kName   = "Envelope";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_CONTROL;
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> attack   {"attack",    0.001f, 0.0f, 0.5f};
    vivid::Param<float> decay    {"decay",     0.2f,   0.01f, 2.0f};
    vivid::Param<float> amplitude{"amplitude", 1.0f,   0.0f, 10.0f};
    vivid::Param<float> offset   {"offset",    0.0f,   0.0f, 10.0f};

    double trigger_time_ = 1000.0;  // large = silent at startup
    float  prev_phase_   = 0.0f;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&attack);
        out.push_back(&decay);
        out.push_back(&amplitude);
        out.push_back(&offset);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"phase", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"value", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        float phase_in = ctx->input_values[0];

        // Trigger detection: phase wraps (drops by > 0.5)
        float delta = phase_in - prev_phase_;
        if (delta < -0.5f) {
            trigger_time_ = 0.0;
        }
        prev_phase_ = phase_in;

        trigger_time_ += ctx->delta_time;

        // Compute envelope
        double env = 0.0;
        double atk = attack.value;
        double dec = decay.value;

        if (trigger_time_ <= atk && atk > 0.0) {
            // Attack phase: linear ramp from 0 to 1
            env = trigger_time_ / atk;
        } else {
            // Decay phase: exponential decay from 1
            double elapsed = trigger_time_ - atk;
            env = std::exp(-elapsed * 5.0 / dec);
        }

        ctx->output_values[0] = static_cast<float>(env) * amplitude.value + offset.value;
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        // Read params: attack=0, decay=1
        float atk = (ctx->param_count > 0) ? ctx->param_values[0] : 0.01f;
        float dec = (ctx->param_count > 1) ? ctx->param_values[1] : 0.2f;
        if (atk < 0.0001f) atk = 0.0001f;
        if (dec < 0.001f) dec = 0.001f;

        float total_time = atk + dec;
        float w = static_cast<float>(ctx->width);
        float h = static_cast<float>(ctx->height);
        float pad = 4.0f;

        // Background
        const uint8_t bg_r = 18, bg_g = 20, bg_b = 23, bg_a = 230;
        // Fill color (domain accent gray-blue)
        const uint8_t fill_r = 100, fill_g = 130, fill_b = 170, fill_a = 160;
        // Curve outline (brighter)
        const uint8_t line_r = 160, line_g = 190, line_b = 220, line_a = 230;
        // Current level indicator
        const uint8_t level_r = 255, level_g = 220, level_b = 100, level_a = 180;

        // Precompute envelope curve: one value per pixel column
        auto env_at = [&](float t) -> float {
            if (t <= atk) return t / atk;
            float elapsed = t - atk;
            return std::exp(-elapsed * 5.0f / dec);
        };

        // Map pixel x to time, envelope y to pixel row
        auto x_to_time = [&](float x) -> float {
            return (x - pad) / (w - 2.0f * pad) * total_time;
        };
        auto env_to_y = [&](float e) -> float {
            return pad + (1.0f - e) * (h - 2.0f * pad);
        };

        // Compute curve y for each column
        std::vector<float> curve_y(ctx->width);
        for (uint32_t x = 0; x < ctx->width; ++x) {
            float t = x_to_time(static_cast<float>(x));
            if (t < 0.0f) t = 0.0f;
            if (t > total_time) t = total_time;
            curve_y[x] = env_to_y(env_at(t));
        }

        // Draw pixels
        for (uint32_t y = 0; y < ctx->height; ++y) {
            uint8_t* row = ctx->pixels + y * ctx->stride;
            float fy = static_cast<float>(y);
            for (uint32_t x = 0; x < ctx->width; ++x) {
                uint8_t* px = row + x * 4;
                float cy = curve_y[x];
                float dist_to_curve = fy - cy;

                if (std::fabs(dist_to_curve) < 1.2f) {
                    // Curve outline
                    px[0] = line_r; px[1] = line_g; px[2] = line_b; px[3] = line_a;
                } else if (dist_to_curve > 0.0f) {
                    // Below curve: filled area
                    px[0] = fill_r; px[1] = fill_g; px[2] = fill_b; px[3] = fill_a;
                } else {
                    // Above curve: background
                    px[0] = bg_r; px[1] = bg_g; px[2] = bg_b; px[3] = bg_a;
                }
            }
        }

        // Draw current-level horizontal indicator from output value
        if (ctx->output_count > 0) {
            float raw = ctx->output_values[0];
            // Normalize: undo offset and amplitude to get 0..1 envelope
            float amp = (ctx->param_count > 2) ? ctx->param_values[2] : 1.0f;
            float off = (ctx->param_count > 3) ? ctx->param_values[3] : 0.0f;
            float env_val = (amp > 0.0001f) ? (raw - off) / amp : 0.0f;
            if (env_val < 0.0f) env_val = 0.0f;
            if (env_val > 1.0f) env_val = 1.0f;

            float ly = env_to_y(env_val);
            uint32_t iy = static_cast<uint32_t>(ly);
            if (iy < ctx->height) {
                uint8_t* row = ctx->pixels + iy * ctx->stride;
                for (uint32_t x = static_cast<uint32_t>(pad);
                     x < ctx->width - static_cast<uint32_t>(pad); ++x) {
                    uint8_t* px = row + x * 4;
                    px[0] = level_r; px[1] = level_g; px[2] = level_b; px[3] = level_a;
                }
            }
        }
    }
};

VIVID_REGISTER(Envelope)
VIVID_THUMBNAIL(Envelope)
