// Test operator: mono audio passthrough that exposes VividAudioContext lane
// metadata as float signal outputs. When lane-lifted, each instance sees its
// own lane_index and the shared lane_count/lane_set_id.
#include "operator_api/operator.h"
#include <cstring>

struct LaneMetadataAudioOp : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName = "LaneMetadataAudioOp";
    static constexpr bool kTimeDependent = false;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",       VIVID_PORT_AUDIO_BUFFER,  VIVID_PORT_INPUT});
        out.push_back({"output",      VIVID_PORT_AUDIO_BUFFER,  VIVID_PORT_OUTPUT});
        out.push_back({"lane_count",  VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"lane_index",  VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"lane_set_id", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_audio(const VividAudioContext* ctx) override {
        // Pass audio through
        if (ctx->input_buffers && ctx->output_buffers)
            std::memcpy(ctx->output_buffers[0], ctx->input_buffers[0],
                        ctx->buffer_size * sizeof(float));
        // Write lane metadata to signal output buffers
        {
            float vals[3];
            vals[0] = static_cast<float>(ctx->lane_count);
            vals[1] = static_cast<float>(ctx->lane_index);
            vals[2] = static_cast<float>(ctx->lane_set_id);
            for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
                for (int j = 0; j < 3; ++j)
                    ctx->output_buffers[j][i] = vals[j];
            }
        }
    }
};

