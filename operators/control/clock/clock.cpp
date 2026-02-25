#include "operator_api/operator.h"
#include <cmath>
#include <cstring>

struct Clock : vivid::OperatorBase {
    static constexpr const char* kName   = "Clock";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_CONTROL;
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> bpm{"bpm", 120.0f, 1.0f, 300.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&bpm);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"beat_phase", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        double beats_per_sec = static_cast<double>(bpm.value) / 60.0;
        double beat_phase = std::fmod(ctx->time * beats_per_sec, 1.0);
        ctx->output_values[0] = static_cast<float>(beat_phase);
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        float phase = ctx->output_values[0];  // beat_phase 0..1

        // Radial fill clock face: circle centered in thumbnail, filled CW from 12 o'clock
        float cx = ctx->width * 0.5f;
        float cy = ctx->height * 0.5f;
        float radius = std::min(cx, cy) - 4.0f;

        // Colors: face = dark background, fill = domain accent (gray-blue)
        const uint8_t face_r = 18, face_g = 20, face_b = 23, face_a = 230;
        const uint8_t fill_r = 100, fill_g = 130, fill_b = 170, fill_a = 200;
        const uint8_t rim_r = 192, rim_g = 200, rim_b = 208, rim_a = 180;

        static constexpr float kPi = 3.14159265f;
        float phase_angle = phase * 2.0f * kPi;  // CW from 12 o'clock

        for (uint32_t y = 0; y < ctx->height; ++y) {
            uint8_t* row = ctx->pixels + y * ctx->stride;
            for (uint32_t x = 0; x < ctx->width; ++x) {
                float dx = static_cast<float>(x) - cx;
                float dy = static_cast<float>(y) - cy;
                float dist = std::sqrt(dx * dx + dy * dy);
                uint8_t* px = row + x * 4;

                if (dist > radius + 1.0f) {
                    // Outside circle: transparent
                    px[0] = px[1] = px[2] = px[3] = 0;
                } else if (dist > radius - 1.0f) {
                    // Rim
                    px[0] = rim_r; px[1] = rim_g; px[2] = rim_b; px[3] = rim_a;
                } else {
                    // Inside circle: check if pixel angle is within filled portion
                    // Angle from 12 o'clock (top), clockwise: atan2(dx, -dy)
                    float angle = std::atan2(dx, -dy);
                    if (angle < 0) angle += 2.0f * kPi;

                    if (angle <= phase_angle) {
                        px[0] = fill_r; px[1] = fill_g; px[2] = fill_b; px[3] = fill_a;
                    } else {
                        px[0] = face_r; px[1] = face_g; px[2] = face_b; px[3] = face_a;
                    }
                }
            }
        }
    }
};

VIVID_REGISTER(Clock)
VIVID_THUMBNAIL(Clock)
