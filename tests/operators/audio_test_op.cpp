// Audio test operator: output[i] = input[i] + level
// With no input connected, input is zeroed, so output = level (constant DC).
#include "operator_api/operator.h"

struct AudioTestOp : vivid::AudioOperatorBase {
    static constexpr const char* kName   = "AudioTestOp";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> level{"level", 0.5f, 0.0f, 10.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&level);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"in",  VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"out", VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process_audio(const VividAudioContext* ctx) override {
        const float* in  = ctx->input_buffers[0];
        float*       out = ctx->output_buffers[0];
        float lv = ctx->param_values[0];

        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            out[i] = in[i] + lv;
        }
    }
};

VIVID_REGISTER(AudioTestOp)
