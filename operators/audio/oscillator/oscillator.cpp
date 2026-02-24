#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct Oscillator : vivid::OperatorBase {
    static constexpr const char* kName   = "Oscillator";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_AUDIO;
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> frequency{"frequency", 440.0f, 20.0f, 20000.0f};
    vivid::Param<float> amplitude{"amplitude", 0.5f, 0.0f, 1.0f};
    vivid::Param<int>   waveform {"waveform",  0,     0,    3};

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
            double sample = 0.0;
            switch (wave) {
                case 0:  // Sine
                    sample = std::sin(phase_ * 2.0 * M_PI);
                    break;
                case 1:  // Saw (rising from -1 to 1)
                    sample = 2.0 * phase_ - 1.0;
                    break;
                case 2:  // Square
                    sample = (phase_ < 0.5) ? 1.0 : -1.0;
                    break;
                case 3:  // Triangle
                    sample = (phase_ < 0.5)
                        ? (4.0 * phase_ - 1.0)
                        : (3.0 - 4.0 * phase_);
                    break;
            }
            out[i] = static_cast<float>(sample) * amp;
            phase_ += phase_inc;
            if (phase_ >= 1.0) phase_ -= 1.0;
        }
    }
};

VIVID_REGISTER(Oscillator)
