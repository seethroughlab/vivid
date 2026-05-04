// Audio-rate registered Smooth operator.
//
// The FrameProcessable embeddable Smooth (used by ChildOp<Smooth> consumers
// e.g. modulated_gain) lives in smooth.h with out-of-line virtuals stubbed
// in smooth_embeddable.cpp. This audio variant is a separate struct
// (SmoothAudio) registered under the same public kName "Smooth".

#include "operator_api/operator.h"
#include <algorithm>
#include <cmath>

namespace {

// Asymmetric exponential smoother: independent rise/fall time constants in
// seconds, applied as `coeff = 1 - exp(-dt / tau)` per sample so audio-rate
// and frame-rate consumers see equivalent perceived smoothing.
struct SmoothState {
    float current_ = 0.0f;
    bool  first_frame_ = true;

    void advance(float target, float dt, float rise_tau, float fall_tau) {
        if (first_frame_) {
            current_ = target;
            first_frame_ = false;
        } else {
            float tau = (target > current_) ? rise_tau : fall_tau;
            if (tau > 0.0001f) {
                float coeff = 1.0f - std::exp(-dt / tau);
                current_ += (target - current_) * coeff;
            } else {
                current_ = target;
            }
        }
    }
};

} // namespace

struct SmoothAudio : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "Smooth";
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> rise_time{"rise_time", 0.1f, 0.0f, 10.0f};
    vivid::Param<float> fall_time{"fall_time", 0.1f, 0.0f, 10.0f};

    SmoothAudio() {
        vivid::semantic_tag(rise_time, "time_seconds");
        vivid::semantic_shape(rise_time, "scalar");
        vivid::semantic_unit(rise_time, "s");

        vivid::semantic_tag(fall_time, "time_seconds");
        vivid::semantic_shape(fall_time, "scalar");
        vivid::semantic_unit(fall_time, "s");

        vivid::description(rise_time, "Smoothing time when the signal is rising, in seconds");
        vivid::description(fall_time, "Smoothing time when the signal is falling, in seconds");

        vivid::layout_row(rise_time, 2, 0);
        vivid::display_hint(rise_time, VIVID_DISPLAY_KNOB);
        vivid::layout_row(fall_time, 2, 1);
        vivid::display_hint(fall_time, VIVID_DISPLAY_KNOB);
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&rise_time);
        out.push_back(&fall_time);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"value", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_audio(const VividAudioContext* ctx) override {
        float sample_dt = 1.0f / static_cast<float>(ctx->sample_rate);
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            float target = ctx->input_buffers[0][i];
            state_.advance(target, sample_dt, rise_time.value, fall_time.value);
            ctx->output_buffers[0][i] = state_.current_;
        }
    }

private:
    SmoothState state_;
};

VIVID_DEFINE_OP(SmoothAudio) {
}

VIVID_REGISTER(SmoothAudio)
