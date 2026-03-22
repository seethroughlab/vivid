#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "operator_api/audio_dsp.h"
#include <cmath>

struct Oscillator : vivid::AudioOperatorBase {
    static constexpr const char* kName   = "Oscillator";
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> frequency{"frequency", 440.0f, 20.0f, 20000.0f};
    vivid::Param<float> amplitude{"amplitude", 0.5f, 0.0f, 1.0f};
    vivid::Param<int>   waveform {"waveform",  0, {"sine", "saw", "square", "triangle"}};

    double phase_ = 0.0;

    Oscillator() {
        vivid::semantic_tag(frequency, "frequency_hz");
        vivid::semantic_shape(frequency, "scalar");
        vivid::semantic_unit(frequency, "Hz");

        vivid::semantic_tag(amplitude, "amplitude_linear");
        vivid::semantic_shape(amplitude, "scalar");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&frequency);
        out.push_back(&amplitude);
        out.push_back(&waveform);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"output",  VIVID_PORT_AUDIO, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 0, 0.0f});
        out.push_back({"freq_cv", VIVID_PORT_SIGNAL, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        out.push_back({"amp_cv",  VIVID_PORT_SIGNAL, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 1.0f});
    }

    void process_audio(const VividAudioContext* ctx) override {
        float* out = ctx->output_buffers[0];
        float freq_cv_val = ctx->input_float_values ? ctx->input_float_values[0] : 0.0f;
        float amp_cv_val  = ctx->input_float_values ? ctx->input_float_values[1] : 1.0f;
        float mod_freq = frequency.value * std::pow(2.0f, freq_cv_val / 12.0f);
        float mod_amp  = amplitude.value * amp_cv_val;
        double phase_inc = static_cast<double>(mod_freq) / ctx->sample_rate;
        int wave = waveform.int_value();

        for (uint32_t i = 0; i < ctx->buffer_size; i++) {
            double sample = audio_dsp::waveform(phase_, wave);
            out[i] = static_cast<float>(sample) * mod_amp;
            phase_ += phase_inc;
            if (phase_ >= 1.0) phase_ -= 1.0;
        }
    }
};

VIVID_REGISTER(Oscillator)
