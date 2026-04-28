// Audio test operator: output[i] = input[i] + level
// With no input connected, input is zeroed, so output = level (constant DC).
#include "operator_api/operator.h"

#include <array>

struct AudioTestOp : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName        = "AudioTestOp";
    // v3 metadata declared so test_operator_loader's deferred-probe test can
    // assert these fields survive scan_deferred() / probe_descriptor().
    static constexpr const char* kDisplayName = "Audio Test Op";
    static constexpr const char* kSummary     = "DC offset for audio testing.";
    static constexpr std::array<const char*, 2> kKeywords = {"test", "audio"};
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> level{"level", 0.5f, 0.0f, 10.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&level);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        VividPortDescriptor in{"in",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT};
        in.semantic_tag = "audio_signal_in";
        in.semantic_shape = "audio_buffer";
        in.semantic_intent = "monitor_input";
        in.description = "Stereo audio input";
        out.push_back(in);

        VividPortDescriptor out_port{"out", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT};
        out_port.semantic_tag = "audio_signal_out";
        out_port.semantic_shape = "audio_buffer";
        out_port.semantic_intent = "monitor_output";
        out_port.description = "Stereo audio output";
        out.push_back(out_port);
        vivid::append_analysis_ports(out);
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
