#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"

struct Gain : vivid::AudioOperatorBase {
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
        out.push_back({"input",  VIVID_PORT_AUDIO, VIVID_PORT_INPUT,  0, 1});
        out.push_back({"output", VIVID_PORT_AUDIO, VIVID_PORT_OUTPUT, 0, 1});
    }

    void process_audio(const VividAudioContext* ctx) override {
        float* in  = ctx->input_buffers[0];
        float* out = ctx->output_buffers[0];
        float g = gain.value;

        for (uint32_t i = 0; i < ctx->buffer_size; i++)
            out[i] = in[i] * g;
    }
};

VIVID_REGISTER(Gain)
