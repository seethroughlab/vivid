#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "operator_api/audio_dsp.h"
#include <cmath>

struct Oscillator : vivid::OperatorBase {
    static constexpr const char* kName   = "Oscillator";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_AUDIO;
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> frequency{"frequency", 440.0f, 20.0f, 20000.0f};
    vivid::Param<float> amplitude{"amplitude", 0.5f, 0.0f, 1.0f};
    vivid::Param<int>   waveform {"waveform",  0, {"sine", "saw", "square", "triangle"}};

    double phase_ = 0.0;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&frequency);
        out.push_back(&amplitude);
        out.push_back(&waveform);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"output", VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        auto* audio = vivid_audio(ctx);
        if (!audio) return;

        float* out = audio->output_buffers[0];
        double phase_inc = static_cast<double>(frequency.value) / audio->sample_rate;
        float amp = amplitude.value;
        int wave = waveform.int_value();

        for (uint32_t i = 0; i < audio->buffer_size; i++) {
            double sample = audio_dsp::waveform(phase_, wave);
            out[i] = static_cast<float>(sample) * amp;
            phase_ += phase_inc;
            if (phase_ >= 1.0) phase_ -= 1.0;
        }
    }
};

VIVID_REGISTER(Oscillator)
