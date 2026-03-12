#pragma once

#include "operator_api/operator.h"
#include <algorithm>
#include <cmath>

struct Smooth : vivid::ControlOperatorBase {
    static constexpr const char* kName   = "Smooth";
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> rise_time{"rise_time", 0.1f, 0.0f, 10.0f};
    vivid::Param<float> fall_time{"fall_time", 0.1f, 0.0f, 10.0f};

    Smooth() {
        vivid::semantic_tag(rise_time, "time_seconds");
        vivid::semantic_shape(rise_time, "scalar");
        vivid::semantic_unit(rise_time, "s");

        vivid::semantic_tag(fall_time, "time_seconds");
        vivid::semantic_shape(fall_time, "scalar");
        vivid::semantic_unit(fall_time, "s");

        // Display as side-by-side knobs
        vivid::layout_row(rise_time, 2, 0);
        vivid::display_hint(rise_time, VIVID_DISPLAY_KNOB);
        vivid::layout_row(fall_time, 2, 1);
        vivid::display_hint(fall_time, VIVID_DISPLAY_KNOB);
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        uint32_t W = ctx->width;
        uint32_t H = ctx->height;

        const uint8_t bg_r = 18,  bg_g = 20,  bg_b = 23,  bg_a = 230;
        const uint8_t st_r = 50,  st_g = 80,  st_b = 90,  st_a = 140; // stepped (dim)
        const uint8_t sm_r = 100, sm_g = 190, sm_b = 200, sm_a = 220; // smooth (bright)

        // Fill background
        for (uint32_t y = 0; y < H; ++y) {
            uint8_t* row = ctx->pixels + y * ctx->stride;
            for (uint32_t x = 0; x < W; ++x) {
                uint8_t* px = row + x * 4;
                px[0] = bg_r; px[1] = bg_g; px[2] = bg_b; px[3] = bg_a;
            }
        }

        // Stepped signal: 4 levels across the width
        //   low → high → mid  (normalized y, 0=top, 1=bottom)
        float steps[4]    = {0.75f, 0.25f, 0.6f, 0.4f};
        uint32_t step_xs[4] = {0, W/3, 2*W/3, W};

        // Draw stepped (source) lines — dim dashed
        for (int s = 0; s < 3; s++) {
            uint32_t x0 = step_xs[s], x1 = step_xs[s+1];
            uint32_t py = static_cast<uint32_t>(steps[s] * (H - 2)) + 1;
            if (py >= H) py = H - 1;
            for (uint32_t x = x0; x < x1; x += 2) {
                uint8_t* px = ctx->pixels + py * ctx->stride + x * 4;
                px[0] = st_r; px[1] = st_g; px[2] = st_b; px[3] = st_a;
            }
            // vertical step at x1
            if (s < 2 && x1 < W) {
                uint32_t py_next = static_cast<uint32_t>(steps[s+1] * (H - 2)) + 1;
                if (py_next >= H) py_next = H - 1;
                uint32_t ylo = std::min(py, py_next), yhi = std::max(py, py_next);
                for (uint32_t y = ylo; y <= yhi; y += 2) {
                    uint8_t* px = ctx->pixels + y * ctx->stride + x1 * 4;
                    px[0] = st_r; px[1] = st_g; px[2] = st_b; px[3] = st_a;
                }
            }
        }

        // Draw smoothed curve — simulate exponential slew with tau ~= W/6 pixels
        float tau = static_cast<float>(W) / 6.0f;
        float cur = steps[0];
        for (uint32_t x = 0; x < W; ++x) {
            float target = steps[0];
            if (x >= step_xs[1]) target = steps[1];
            if (x >= step_xs[2]) target = steps[2];

            float coeff = 1.0f - std::exp(-1.0f / tau);
            cur += (target - cur) * coeff;

            uint32_t py = static_cast<uint32_t>(cur * (H - 2)) + 1;
            if (py >= H) py = H - 1;
            uint8_t* px = ctx->pixels + py * ctx->stride + x * 4;
            px[0] = sm_r; px[1] = sm_g; px[2] = sm_b; px[3] = sm_a;
            // thicken by one pixel
            if (py + 1 < H) {
                uint8_t* px2 = ctx->pixels + (py+1) * ctx->stride + x * 4;
                px2[0] = sm_r; px2[1] = sm_g; px2[2] = sm_b; px2[3] = sm_a / 2;
            }
        }
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&rise_time);
        out.push_back(&fall_time);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input", VIVID_PORT_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"value", VIVID_PORT_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        float target = ctx->input_values[0];

        if (first_frame_) {
            current_ = target;
            first_frame_ = false;
        } else {
            float dt = static_cast<float>(ctx->delta_time);
            float tau = (target > current_) ? rise_time.value : fall_time.value;
            if (tau > 0.0001f) {
                float coeff = 1.0f - std::exp(-dt / tau);
                current_ += (target - current_) * coeff;
            } else {
                current_ = target;
            }
        }

        ctx->output_values[0] = current_;
    }

private:
    float current_ = 0.0f;
    bool first_frame_ = true;
};
