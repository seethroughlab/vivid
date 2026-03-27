#include "operator_api/operator.h"

struct Gain : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "Gain";
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> gain{"gain", 1.0f, 0.0f, 2.0f};

    Gain() {
        vivid::semantic_tag(gain, "amplitude_linear");
        vivid::semantic_shape(gain, "scalar");
        vivid::semantic_intent(gain, "input_gain");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&gain);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",        VIVID_PORT_AUDIO, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"output",       VIVID_PORT_AUDIO, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"amplitude_cv", VIVID_PORT_SIGNAL, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 1.0f});
    }

    void process_audio(const VividAudioContext* ctx) override {
        float* in  = ctx->input_buffers[0];
        float* out = ctx->output_buffers[0];
        float amp_cv_val = ctx->input_float_values ? ctx->input_float_values[0] : 1.0f;
        float g = gain.value * amp_cv_val;

        for (uint32_t i = 0; i < ctx->buffer_size; i++)
            out[i] = in[i] * g;
    }
};

VIVID_REGISTER(Gain)
