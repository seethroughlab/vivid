// Audio test operator: outputs a constant DC signal equal to the FLOAT cv input.
#include "operator_api/operator.h"

struct AudioFloatCvOp : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName = "AudioFloatCvOp";
    static constexpr bool kTimeDependent = false;

    void collect_params(std::vector<vivid::ParamBase*>&) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"cv", VIVID_PORT_SCALAR, VIVID_PORT_INPUT,
                       VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        out.push_back({"out", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT});
        vivid::append_analysis_ports(out);
    }

    void process_audio(const VividAudioContext* ctx) override {
        const float cv = ctx->input_float_values ? ctx->input_float_values[0] : 0.0f;
        float* out = ctx->output_buffers[0];
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) out[i] = cv;
    }
};

VIVID_REGISTER(AudioFloatCvOp)
