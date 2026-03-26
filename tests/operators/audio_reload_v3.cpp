// Audio hot-reload test operator v3: same descriptor as v2, different behavior.
#include "operator_api/operator.h"

struct AudioReloadOp : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName = "AudioReloadOp";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> level{"level", 0.5f, 0.0f, 100.0f};
    vivid::Param<float> offset{"offset", 1.0f, -100.0f, 100.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&level);
        out.push_back(&offset);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"out", VIVID_PORT_AUDIO, VIVID_PORT_OUTPUT});
    }

    void process_audio(const VividAudioContext* ctx) override {
        float* out = ctx->output_buffers[0];
        const float value = ctx->param_values[0] * 4.0f + ctx->param_values[1];
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) out[i] = value;
    }
};

VIVID_REGISTER(AudioReloadOp)
