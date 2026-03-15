// Audio hot-reload test operator v1: output = level * 2.0
#include "operator_api/operator.h"

struct AudioReloadOp : vivid::AudioOperatorBase {
    static constexpr const char* kName = "AudioReloadOp";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> level{"level", 0.5f, 0.0f, 100.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&level);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"out", VIVID_PORT_AUDIO, VIVID_PORT_OUTPUT});
    }

    void process_audio(const VividAudioContext* ctx) override {
        float* out = ctx->output_buffers[0];
        const float value = ctx->param_values[0] * 2.0f;
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) out[i] = value;
    }
};

VIVID_REGISTER(AudioReloadOp)
