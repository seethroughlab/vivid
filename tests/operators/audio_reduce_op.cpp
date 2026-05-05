// Test operator: reduction-stage audio consumer.
//
// Accepts an auto-width audio input, sums every input channel to mono, and
// writes the result to a mono output buffer. This gives compiler tests a small
// reduction-stage audio node without depending on package operators.

#include "operator_api/operator.h"

struct AudioReduceOp : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName = "AudioReduceOp";
    static constexpr bool kTimeDependent = false;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_REDUCTION;

    void collect_params(std::vector<vivid::ParamBase*>&) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT,
                       VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 0, 0.0f});
        out.push_back({"output", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT,
                       VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
    }

    void process_audio(const VividAudioContext* ctx) override {
        const float* in = ctx->input_buffers[0];
        float* out = ctx->output_buffers[0];
        uint32_t channels = ctx->input_channel_counts ? ctx->input_channel_counts[0] : 1;
        if (channels == 0) channels = 1;

        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            float sum = 0.0f;
            for (uint32_t ch = 0; ch < channels; ++ch)
                sum += in[ch * ctx->buffer_size + i];
            out[i] = sum;
        }
    }
};

