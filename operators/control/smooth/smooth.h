#pragma once

#include "operator_api/operator.h"
#include <algorithm>
#include <cmath>

struct SmoothThumbState;

// Smooth — dual-cadence control operator.
//
// Inherits both FrameProcessable and AudioProcessable, making it audio-capable.
// State: first-order exponential smoothing with separate rise/fall time constants.
//
struct Smooth : vivid::OperatorBase, vivid::FrameProcessable, vivid::AudioProcessable {
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

    ~Smooth() override;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&rise_time);
        out.push_back(&fall_time);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input", VIVID_PORT_SIGNAL, VIVID_PORT_INPUT});
        out.push_back({"value", VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});
    }

    void advance(float target, float dt) {
        if (first_frame_) {
            current_ = target;
            first_frame_ = false;
        } else {
            float tau = (target > current_) ? rise_time.value : fall_time.value;
            if (tau > 0.0001f) {
                float coeff = 1.0f - std::exp(-dt / tau);
                current_ += (target - current_) * coeff;
            } else {
                current_ = target;
            }
        }
    }

    void process_frame(const VividFrameContext* ctx) override {
        advance(ctx->input_values[0], static_cast<float>(ctx->delta_time));
        ctx->output_values[0] = current_;
    }

    void process_audio(const VividAudioContext* ctx) override {
        float target = ctx->input_float_values[0];
        float sample_dt = 1.0f / static_cast<float>(ctx->sample_rate);
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            advance(target, sample_dt);
            ctx->output_buffers[0][i] = current_;
        }
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override;

private:
    float current_ = 0.0f;
    bool first_frame_ = true;
    SmoothThumbState* thumb_state_ = nullptr;

    void rebuild_thumb_pipeline(const VividThumbnailContext* ctx);
};
