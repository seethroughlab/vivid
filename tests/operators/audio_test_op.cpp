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
        out.push_back({"in",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT,  0, 0, nullptr, 0, 0.0f, nullptr, "audio_signal_in",  "audio_buffer", "monitor_input",  "Stereo audio input"});
        out.push_back({"out", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT, 0, 0, nullptr, 0, 0.0f, nullptr, "audio_signal_out", "audio_buffer", "monitor_output", "Stereo audio output"});
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
