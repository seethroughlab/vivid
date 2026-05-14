#include "operator_api/operator.h"

#include <cmath>

// ---------------------------------------------------------------------------
// Distortion: tanh soft-clipping with tone control (mono)
// ---------------------------------------------------------------------------

struct OnePole {
    float a  = 0.0f;
    float b  = 1.0f;
    float z1 = 0.0f;

    void set_cutoff(float freq, float sr) {
        float x = std::exp(-2.0f * 3.14159265f * freq / sr);
        a = 1.0f - x;
        b = x;
    }

    float process(float input) {
        z1 = input * a + z1 * b;
        return z1;
    }
};

/**
 * @brief Tanh soft-clipping distortion with post-drive tone filter.
 *
 * Applies waveshaping distortion via hyperbolic tangent saturation
 * followed by a one-pole tone filter sweepable from 1-10 kHz.
 *
 * @param drive Amount of gain before clipping. Higher values = more harmonics.
 * @param tone Post-distortion brightness. 0 = dark, 1 = bright.
 * @see Bitcrush, RingMod, Filter
 */
struct Distortion : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "Distortion";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> drive{"drive", 3.0f, 1.0f, 10.0f};
    vivid::Param<float> tone {"tone",  0.5f, 0.0f,  1.0f};
    vivid::Param<float> level{"level", 0.8f, 0.0f,  2.0f};
    vivid::Param<float> mix  {"mix",   1.0f, 0.0f,  1.0f};

    static constexpr uint32_t kMaxChannels = 2;
    OnePole tone_filter_[kMaxChannels];

    Distortion() {
        vivid::semantic_tag(drive, "amplitude_linear");
        vivid::semantic_shape(drive, "scalar");
        vivid::semantic_intent(drive, "pre_gain");
        vivid::description(drive, "Gain before clipping (higher = more harmonics)");

        vivid::semantic_tag(tone, "probability_01");
        vivid::semantic_shape(tone, "scalar");
        vivid::description(tone, "Post-distortion brightness (0 = dark, 1 = bright)");

        vivid::semantic_tag(level, "amplitude_linear");
        vivid::semantic_shape(level, "scalar");
        vivid::semantic_intent(level, "post_gain");
        vivid::description(level, "Output level after distortion");

        vivid::semantic_tag(mix, "probability_01");
        vivid::semantic_shape(mix, "scalar");
        vivid::semantic_intent(mix, "wet_mix");
        vivid::description(mix, "Dry/wet blend (0 = clean, 1 = fully distorted)");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&drive);
        out.push_back(&tone);
        out.push_back(&level);
        out.push_back(&mix);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 0, 0.0f});
        out.push_back({"output", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 0, 0.0f});
        vivid::append_analysis_ports(out);
    }

    void process_audio(const VividAudioContext* ctx) override {
        uint32_t nch = ctx->input_channel_counts ? ctx->input_channel_counts[0] : 1u;
        if (nch > kMaxChannels) nch = kMaxChannels;
        uint32_t frames = ctx->buffer_size;

        float d   = drive.value;
        float lev = level.value;
        float wet = mix.value;
        float dry = 1.0f - wet;

        float cutoff = 1000.0f + tone.value * 9000.0f;
        float norm = 1.0f / std::tanh(d);

        for (uint32_t c = 0; c < nch; c++) {
            const float* in_c  = ctx->input_buffers[0]  + c * frames;
            float*       out_c = ctx->output_buffers[0] + c * frames;
            tone_filter_[c].set_cutoff(cutoff, static_cast<float>(ctx->sample_rate));
            for (uint32_t i = 0; i < frames; i++) {
                float saturated = std::tanh(in_c[i] * d) * norm;
                float filtered  = tone_filter_[c].process(saturated);
                out_c[i] = in_c[i] * dry + filtered * lev * wet;
            }
        }
    }
};

VIVID_DEFINE_OP(Distortion) {
}

