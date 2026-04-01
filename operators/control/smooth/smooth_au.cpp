#include "smooth_core.h"

struct SmoothAu : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "smooth_au";
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> rise_time{"rise_time", 0.1f, 0.0f, 10.0f};
    vivid::Param<float> fall_time{"fall_time", 0.1f, 0.0f, 10.0f};

    SmoothAu() {
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
        float target = ctx->input_float_values[0];
        float sample_dt = 1.0f / static_cast<float>(ctx->sample_rate);
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            core_.advance(target, sample_dt, rise_time.value, fall_time.value);
            ctx->output_buffers[0][i] = core_.current_;
        }
    }

private:
    SmoothCore core_;
};

VIVID_REGISTER(SmoothAu)
