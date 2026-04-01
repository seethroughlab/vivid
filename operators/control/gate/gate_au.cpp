#include "operator_api/operator.h"
/**
 * @brief Passes or blocks a signal based on a gate threshold (audio-rate).
 *
 * When the gate input exceeds the threshold, the signal passes through.
 * Otherwise outputs zero. Optional invert mode reverses the logic.
 *
 * @see Logic, SampleHold, Math
 */
struct GateAu : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "gate_au";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> threshold{"threshold", 0.5f, 0.0f, 1.0f};
    vivid::Param<bool>  invert{"invert", false};

    GateAu() {
        vivid::description(threshold, "Gate input level required to pass the signal (0 to 1)");
        vivid::description(invert, "Reverses the gate logic so signal passes when input is below threshold");

        vivid::semantic_tag(threshold, "probability_01");
        vivid::semantic_shape(threshold, "scalar");

        vivid::semantic_tag(invert, "enabled");
        vivid::semantic_shape(invert, "bool");
        vivid::semantic_intent(invert, "invert_logic");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&threshold);
        out.push_back(&invert);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"signal", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"gate",   VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"value",  VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"open",   VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_audio(const VividAudioContext* ctx) override {
        float signal = ctx->input_float_values[0];
        bool is_open = ctx->input_float_values[1] > threshold.value;
        if (invert.bool_value()) is_open = !is_open;

        float value = is_open ? signal : 0.0f;
        float open_val = is_open ? 1.0f : 0.0f;
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            ctx->output_buffers[0][i] = value;
            ctx->output_buffers[1][i] = open_val;
        }
    }
};

VIVID_REGISTER(GateAu)
