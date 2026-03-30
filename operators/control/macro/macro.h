#pragma once

#include "operator_api/operator.h"
/**
 * @brief Simple scalar transform mapping a value through amplitude and offset.
 *
 * Outputs `value * amplitude + offset`. Useful for rescaling control
 * signals to a desired range, or as a manually-tweakable control source.
 *
 * @see Math, Smooth, LFO
 */
struct Macro : vivid::OperatorBase, vivid::FrameProcessable, vivid::AudioProcessable {
    static constexpr const char* kName   = "Macro";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> value     {"value",     0.5f, 0.0f, 1.0f};
    vivid::Param<float> amplitude {"amplitude", 1.0f, 0.0f, 10.0f};
    vivid::Param<float> offset    {"offset",    0.0f, -10.0f, 10.0f};

    Macro() {
        vivid::semantic_tag(value, "amplitude_linear");
        vivid::semantic_shape(value, "scalar");

        vivid::semantic_tag(amplitude, "amplitude_linear");
        vivid::semantic_shape(amplitude, "scalar");
        vivid::semantic_intent(amplitude, "env_amount");

        vivid::semantic_tag(offset, "amplitude_linear");
        vivid::semantic_shape(offset, "scalar");
        vivid::semantic_intent(offset, "dc_offset");

        vivid::description(value, "Base control value before scaling (0–1)");
        vivid::description(amplitude, "Multiplier applied to the base value");
        vivid::description(offset, "Constant added after scaling");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        display_hint(value, VIVID_DISPLAY_KNOB);
        // value: full-width by default

        out.push_back(&value);
        out.push_back(&amplitude);
        out.push_back(&offset);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"gate",       VIVID_PORT_SIGNAL, VIVID_PORT_INPUT});
        out.push_back({"beat_phase", VIVID_PORT_SIGNAL, VIVID_PORT_INPUT});
        out.push_back({"value",      VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        ctx->output_values[0] = value.value * amplitude.value + offset.value;
    }

    void process_audio(const VividAudioContext* ctx) override {
        float result = value.value * amplitude.value + offset.value;
        for (uint32_t i = 0; i < ctx->buffer_size; ++i)
            ctx->output_buffers[0][i] = result;
    }
};
