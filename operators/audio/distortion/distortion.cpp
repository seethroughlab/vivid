#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"

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

struct Distortion : vivid::AudioOperatorBase {
    static constexpr const char* kName   = "Distortion";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> drive{"drive", 3.0f, 1.0f, 10.0f};
    vivid::Param<float> tone {"tone",  0.5f, 0.0f,  1.0f};
    vivid::Param<float> level{"level", 0.8f, 0.0f,  2.0f};
    vivid::Param<float> mix  {"mix",   1.0f, 0.0f,  1.0f};

    OnePole tone_filter_;

    Distortion() {
        vivid::semantic_tag(drive, "amplitude_linear");
        vivid::semantic_shape(drive, "scalar");
        vivid::semantic_intent(drive, "pre_gain");

        vivid::semantic_tag(tone, "probability_01");
        vivid::semantic_shape(tone, "scalar");

        vivid::semantic_tag(level, "amplitude_linear");
        vivid::semantic_shape(level, "scalar");
        vivid::semantic_intent(level, "post_gain");

        vivid::semantic_tag(mix, "probability_01");
        vivid::semantic_shape(mix, "scalar");
        vivid::semantic_intent(mix, "wet_mix");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&drive);
        out.push_back(&tone);
        out.push_back(&level);
        out.push_back(&mix);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_AUDIO, VIVID_PORT_INPUT});
        out.push_back({"output", VIVID_PORT_AUDIO, VIVID_PORT_OUTPUT});
    }

    void process_audio(const VividAudioContext* ctx) override {
        float* in  = ctx->input_buffers[0];
        float* out = ctx->output_buffers[0];
        uint32_t frames = ctx->buffer_size;

        float d   = drive.value;
        float lev = level.value;
        float wet = mix.value;
        float dry = 1.0f - wet;

        float cutoff = 1000.0f + tone.value * 9000.0f;
        tone_filter_.set_cutoff(cutoff, static_cast<float>(ctx->sample_rate));

        float norm = 1.0f / std::tanh(d);

        for (uint32_t i = 0; i < frames; i++) {
            float saturated = std::tanh(in[i] * d) * norm;
            float filtered  = tone_filter_.process(saturated);
            float processed = filtered * lev;
            out[i] = in[i] * dry + processed * wet;
        }
    }
};

VIVID_REGISTER(Distortion)
