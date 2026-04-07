// Audio test operator for audio-frame bridge lane snapshot testing.
// Reads a lane-array input, copies it to the lane-array output, and emits a
// DC audio signal equal to the sum of the lane values.
#include "operator_api/operator.h"
#include <cstring>

struct AudioLaneOp : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "AudioLaneOp";
    static constexpr bool kTimeDependent = false;

    void collect_params(std::vector<vivid::ParamBase*>&) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"values", VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT});
        out.push_back({"out",    VIVID_PORT_AUDIO_BUFFER,    VIVID_PORT_OUTPUT});
        out.push_back({"echo",   VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});
        vivid::append_analysis_ports(out);
    }

    void process_audio(const VividAudioContext* ctx) override {
        // Sum lane input values
        float sum = 0.0f;
        if (ctx->input_lanes) {
            const auto& isp = ctx->input_lanes[0];  // "values" input (port 0)
            for (uint32_t i = 0; i < isp.length; ++i) {
                sum += isp.data[i];
            }

            // Echo lane array to output port 1 ("echo")
            if (ctx->output_lanes) {
                auto& osp = ctx->output_lanes[1];  // "echo" output (port 1)
                float* buf = osp.resize(osp.handle, isp.length);
                if (buf && isp.length > 0) {
                    std::memcpy(buf, isp.data, isp.length * sizeof(float));
                    osp.commit(osp.handle, isp.length);
                }
            }
        }

        // Write DC audio output = sum
        float* out = ctx->output_buffers[0];  // "out" output (port 0)
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            out[i] = sum;
        }
    }
};

VIVID_REGISTER(AudioLaneOp)
