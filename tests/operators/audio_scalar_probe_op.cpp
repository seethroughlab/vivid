// Audio test probe: converts a scalar input into a constant DC audio output so
// bridge and scalar-routing behavior can be verified through normal analysis.
#include "operator_api/operator.h"
#include <cmath>

struct AudioScalarProbeOp : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName = "AudioScalarProbeOp";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> held{"held", 0.0f, -1000.0f, 1000.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&held);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"value", VIVID_PORT_SCALAR, VIVID_PORT_INPUT,
                       VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        out.push_back({"out", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT});
        vivid::append_analysis_ports(out);
    }

    void process_audio(const VividAudioContext* ctx) override {
        float scalar_value = held.value;
        if (ctx->input_buffers && ctx->input_buffers[0]) {
            const float* in = ctx->input_buffers[0];
            for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
                if (std::fabs(in[i]) > 1e-6f) {
                    scalar_value = in[i];
                    break;
                }
            }
        }

        float* out = ctx->output_buffers[0];
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            out[i] = scalar_value;
        }
    }
};

