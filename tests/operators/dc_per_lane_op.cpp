// Test operator: per-lane DC audio source.
//
// Strategy-independent audio operator that outputs a distinct DC value
// per lane: DC = (lane_index + 1) * 0.1f. When assigned LoopBased from
// a structural lane source upstream, each lane iteration writes its own DC
// value to the output buffer.
//
// Used to provide per-lane-distinct audio input for cross-strategy
// equivalence testing.

#include "operator_api/operator.h"

struct DcPerLaneOp : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName = "DcPerLaneOp";
    static constexpr bool kTimeDependent = false;
    static constexpr bool kStrategyIndependent = true;

    void collect_params(std::vector<vivid::ParamBase*>&) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT});
        out.push_back({"output", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT});
        // Spread input to receive structural upstream (triggers LoopBased)
        out.push_back({.name="lanes", .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_INPUT, .multiplicity=VIVID_MULTIPLICITY_MANY});
    }

    void process_audio(const VividAudioContext* ctx) override {
        float dc = static_cast<float>(ctx->lane_index + 1) * 0.1f;
        float* out = ctx->output_buffers[0];
        for (uint32_t i = 0; i < ctx->buffer_size; ++i)
            out[i] = dc;
    }
};

