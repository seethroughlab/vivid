#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "operator_api/audio_dsp.h"

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------------------
// Ring Modulator — multiplies input by internal carrier oscillator (mono)
// ---------------------------------------------------------------------------

struct RingMod : vivid::AudioOperatorBase {
    static constexpr const char* kName   = "RingMod";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> carrier_freq{"carrier_freq", 440.0f, 20.0f, 20000.0f};
    vivid::Param<int>   carrier_waveform{"carrier_waveform", 0, {"Sine", "Saw", "Square", "Triangle"}};
    vivid::Param<float> mix{"mix", 0.5f, 0.0f, 1.0f};

    double phase_ = 0.0;

    RingMod() {
        vivid::semantic_tag(carrier_freq, "frequency_hz");
        vivid::semantic_shape(carrier_freq, "scalar");
        vivid::semantic_unit(carrier_freq, "Hz");
        vivid::display_hint(carrier_freq, VIVID_DISPLAY_KNOB);

        vivid::display_hint(carrier_waveform, VIVID_DISPLAY_KNOB);

        vivid::semantic_tag(mix, "probability_01");
        vivid::semantic_shape(mix, "scalar");
        vivid::semantic_intent(mix, "wet_mix");
        vivid::display_hint(mix, VIVID_DISPLAY_KNOB);
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&carrier_freq);
        out.push_back(&carrier_waveform);
        out.push_back(&mix);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",   VIVID_PORT_AUDIO, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"output",  VIVID_PORT_AUDIO, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"freq_cv", VIVID_PORT_FLOAT, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SCALAR, 0, nullptr, 0, 0.0f});
    }

    void process_audio(const VividAudioContext* ctx) override {
        float* in  = ctx->input_buffers[0];
        float* out = ctx->output_buffers[0];
        uint32_t frames = ctx->buffer_size;

        float freq_cv = ctx->input_float_values ? ctx->input_float_values[0] : 0.0f;
        float freq = carrier_freq.value * std::pow(2.0f, freq_cv / 12.0f);
        if (freq < 20.0f)    freq = 20.0f;
        if (freq > 20000.0f) freq = 20000.0f;

        int   wf  = carrier_waveform.int_value();
        float wet = mix.value;
        float dry = 1.0f - wet;
        double inv_sr = 1.0 / static_cast<double>(ctx->sample_rate);

        for (uint32_t i = 0; i < frames; i++) {
            float carrier = static_cast<float>(audio_dsp::waveform(phase_, wf));
            float wet_sig = in[i] * carrier;
            out[i] = in[i] * dry + wet_sig * wet;

            phase_ += static_cast<double>(freq) * inv_sr;
            if (phase_ >= 1.0) phase_ -= 1.0;
        }
    }
};

VIVID_REGISTER(RingMod)
