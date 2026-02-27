#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"

#include <cmath>

// ---------------------------------------------------------------------------
// Bitcrusher: bit-depth reduction + sample-rate reduction (mono)
// ---------------------------------------------------------------------------

struct Bitcrush : vivid::OperatorBase {
    static constexpr const char* kName   = "Bitcrush";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_AUDIO;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> bits{"bits",  8.0f, 1.0f, 16.0f};
    vivid::Param<float> rate{"rate",  8000.0f, 100.0f, 48000.0f};
    vivid::Param<float> mix {"mix",   1.0f, 0.0f, 1.0f};

    float hold_    = 0.0f; // sample-and-hold value
    float counter_ = 0.0f; // fractional sample counter

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&bits);
        out.push_back(&rate);
        out.push_back(&mix);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"output", VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        auto* audio = vivid_audio(ctx);
        if (!audio) return;

        float* in  = audio->input_buffers[0];
        float* out = audio->output_buffers[0];
        uint32_t frames = audio->buffer_size;

        float target_rate = rate.value;
        float sr = static_cast<float>(audio->sample_rate);
        float ratio = sr / target_rate;
        if (ratio < 1.0f) ratio = 1.0f;

        float levels = std::pow(2.0f, bits.value);
        float wet = mix.value;
        float dry = 1.0f - wet;

        for (uint32_t i = 0; i < frames; i++) {
            counter_ += 1.0f;
            if (counter_ >= ratio) {
                counter_ -= ratio;
                // Sample-and-hold: grab new value and quantize
                float v = in[i];
                v = std::round(v * levels) / levels;
                hold_ = v;
            }
            out[i] = in[i] * dry + hold_ * wet;
        }
    }
};

VIVID_REGISTER(Bitcrush)
