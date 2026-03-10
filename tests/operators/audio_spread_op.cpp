// Audio-domain test operator for cross-domain spread testing.
// Reads CONTROL_SPREAD input, copies to CONTROL_SPREAD output,
// generates DC audio output = sum of spread values.
#include "operator_api/operator.h"
#include <cstring>

struct AudioSpreadOp : vivid::AudioOperatorBase {
    static constexpr const char* kName   = "AudioSpreadOp";
    static constexpr bool kTimeDependent = false;

    void collect_params(std::vector<vivid::ParamBase*>&) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"values", VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});
        out.push_back({"out",    VIVID_PORT_AUDIO_FLOAT,    VIVID_PORT_OUTPUT});
        out.push_back({"echo",   VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_OUTPUT});
    }

    void process_audio(const VividAudioContext* ctx) override {
        // Sum spread input values
        float sum = 0.0f;
        if (ctx->input_spreads) {
            const auto& isp = ctx->input_spreads[0];  // "values" input (port 0)
            for (uint32_t i = 0; i < isp.length; ++i) {
                sum += isp.data[i];
            }

            // Echo spread to output port 1 ("echo")
            if (ctx->output_spreads) {
                auto& osp = ctx->output_spreads[1];  // "echo" output (port 1)
                if (osp.capacity >= isp.length) {
                    osp.length = isp.length;
                    if (isp.length > 0) {
                        std::memcpy(osp.data, isp.data, isp.length * sizeof(float));
                    }
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

VIVID_REGISTER(AudioSpreadOp)
