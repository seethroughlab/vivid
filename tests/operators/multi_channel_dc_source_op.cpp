// Test operator: 4-channel DC audio source.
//
// Outputs a 4-channel audio buffer where channel c = (c+1) * 0.1f.
// Used to trigger InstancePerLane on downstream mono-declared operators
// with per-lane-distinct DC values matching DcPerLaneOp's output.

#include "operator_api/operator.h"

struct MultiChannelDcSourceOp : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName = "MultiChannelDcSourceOp";
    static constexpr bool kTimeDependent = false;

    void collect_params(std::vector<vivid::ParamBase*>&) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"output", VIVID_PORT_AUDIO, VIVID_PORT_OUTPUT,
                        VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 4, 0.0f});
    }

    void process_audio(const VividAudioContext* ctx) override {
        for (uint32_t ch = 0; ch < 4; ++ch) {
            float* out = ctx->output_buffers[0] + ch * ctx->buffer_size;
            float dc = static_cast<float>(ch + 1) * 0.1f;
            for (uint32_t i = 0; i < ctx->buffer_size; ++i)
                out[i] = dc;
        }
    }
};

VIVID_REGISTER(MultiChannelDcSourceOp)
