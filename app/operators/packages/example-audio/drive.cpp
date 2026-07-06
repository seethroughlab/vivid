// Example package operator (kind: audio_effect): a soft-saturation overdrive.
// Compiled at install time by the package compiler (clang++) into a loadable .dylib — no app
// rebuild, no wgpu (audio operators declare kind "audio_effect", so the manifest links no GPU).
// Self-contained against operator_api/operator.h. An audio EFFECT has one stereo audio input and
// one stereo audio output; the host feeds the input and reads the output each block.
#include "operator_api/operator.h"

#include <array>
#include <cmath>
#include <vector>

namespace {
VividPortDescriptor aud_port(const char* name, VividPortDirection dir) {
    VividPortDescriptor p{};
    p.name = name; p.type = VIVID_PORT_AUDIO_BUFFER; p.direction = dir;
    p.value_type = VIVID_VALUE_AUDIO; p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
    p.channels = 2;   // stereo — the shape the audio runtime feeds (see descriptor validation)
    return p;
}
}  // namespace

struct DriveOp : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName = "Drive";
    static constexpr const char* kDisplayName = "Drive";
    static constexpr const char* kSummary = "Soft-saturation overdrive (example audio-effect package operator).";
    static constexpr std::array<const char*, 3> kKeywords = { "audio", "effect", "drive" };

    vivid::Param<float> drive{ "drive", 2.0f, 1.0f, 20.0f };   // pre-gain into the saturator
    vivid::Param<float> mix{ "mix", 1.0f, 0.0f, 1.0f };        // dry/wet

    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        // Semantic metadata makes the op self-describing to agents + inspectors (see discovery).
        vivid::semantic_intent(drive, "overdrive amount");
        vivid::description(drive, "pre-gain driven into the saturator");
        drive.display_hint = VIVID_DISPLAY_KNOB;
        vivid::semantic_shape(mix, "scalar");
        vivid::semantic_intent(mix, "dry/wet mix");
        o.push_back(&drive); o.push_back(&mix);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        o.push_back(aud_port("input", VIVID_PORT_INPUT));
        o.push_back(aud_port("output", VIVID_PORT_OUTPUT));
    }

    // Audio-thread callback: must be RT-safe (no allocation, no locks). Reads the current param
    // values from the context (host may be automating them) and writes tanh(in * drive) blended
    // with dry by `mix`, per channel.
    void process_audio(const VividAudioContext* c) override {
        if (!c->input_buffers || !c->output_buffers) return;
        const uint32_t N = c->buffer_size;
        const uint8_t nch = c->input_channel_counts ? c->input_channel_counts[0] : 2;
        const float d = c->param_values ? c->param_values[0] : drive.value;
        const float m = c->param_values ? c->param_values[1] : mix.value;
        for (uint8_t ch = 0; ch < nch && ch < 2; ++ch) {
            const float* in  = c->input_buffers[0]  + ch * N;
            float*       out = c->output_buffers[0] + ch * N;
            for (uint32_t i = 0; i < N; ++i) {
                const float wet = std::tanh(in[i] * d);
                out[i] = in[i] * (1.0f - m) + wet * m;
            }
        }
    }
};

VIVID_REGISTER(DriveOp)
