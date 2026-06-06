// Test operator: mono DC audio source (constant 0.5). Single channel keeps a
// downstream Pointwise op on the Scalar (non-lifted) audio path.
#include "operator_api/operator.h"

struct MonoDcSourceOp : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName = "MonoDcSourceOp";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> level{"level", 0.5f, 0.0f, 1.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override { out.push_back(&level); }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"output", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT});
    }

    void process_audio(const VividAudioContext* ctx) override {
        float* out = ctx->output_buffers[0];
        const float v = ctx->param_values[0];
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) out[i] = v;
    }
};
