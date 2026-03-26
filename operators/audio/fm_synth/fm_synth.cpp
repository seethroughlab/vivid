#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "operator_api/adsr.h"

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------------------
// FM Synth — 2-operator FM synthesizer with ADSR envelope (mono, generator)
// ---------------------------------------------------------------------------

struct FmSynth : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "FmSynth";
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> carrier_freq{"carrier_freq", 440.0f, 20.0f, 20000.0f};
    vivid::Param<float> mod_ratio   {"mod_ratio",    2.0f,   0.0f,  16.0f};
    vivid::Param<float> mod_index   {"mod_index",    1.0f,   0.0f,  20.0f};
    vivid::Param<float> attack      {"attack",       0.01f,  0.001f, 5.0f};
    vivid::Param<float> decay       {"decay",        0.2f,   0.001f, 5.0f};
    vivid::Param<float> sustain     {"sustain",      0.7f,   0.0f,   1.0f};
    vivid::Param<float> release     {"release",      0.3f,   0.001f, 5.0f};
    vivid::Param<float> amplitude   {"amplitude",    0.5f,   0.0f,   1.0f};

    double carrier_phase_ = 0.0;
    double mod_phase_     = 0.0;
    vivid::adsr::State env_state_;
    float prev_gate_      = 0.0f;

    FmSynth() {
        vivid::semantic_tag(carrier_freq, "frequency_hz");
        vivid::semantic_shape(carrier_freq, "scalar");
        vivid::semantic_unit(carrier_freq, "Hz");
        vivid::display_hint(carrier_freq, VIVID_DISPLAY_KNOB);

        vivid::semantic_shape(mod_ratio, "scalar");
        vivid::display_hint(mod_ratio, VIVID_DISPLAY_KNOB);

        vivid::semantic_tag(mod_index, "amplitude_linear");
        vivid::semantic_shape(mod_index, "scalar");
        vivid::display_hint(mod_index, VIVID_DISPLAY_KNOB);

        vivid::semantic_tag(attack, "time_seconds");
        vivid::semantic_shape(attack, "scalar");
        vivid::semantic_unit(attack, "s");
        vivid::display_hint(attack, VIVID_DISPLAY_KNOB);

        vivid::semantic_tag(decay, "time_seconds");
        vivid::semantic_shape(decay, "scalar");
        vivid::semantic_unit(decay, "s");
        vivid::display_hint(decay, VIVID_DISPLAY_KNOB);

        vivid::semantic_tag(sustain, "probability_01");
        vivid::semantic_shape(sustain, "scalar");
        vivid::display_hint(sustain, VIVID_DISPLAY_KNOB);

        vivid::semantic_tag(release, "time_seconds");
        vivid::semantic_shape(release, "scalar");
        vivid::semantic_unit(release, "s");
        vivid::display_hint(release, VIVID_DISPLAY_KNOB);

        vivid::semantic_tag(amplitude, "amplitude_linear");
        vivid::semantic_shape(amplitude, "scalar");
        vivid::display_hint(amplitude, VIVID_DISPLAY_KNOB);
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&carrier_freq);
        out.push_back(&mod_ratio);
        out.push_back(&mod_index);
        out.push_back(&attack);
        out.push_back(&decay);
        out.push_back(&sustain);
        out.push_back(&release);
        out.push_back(&amplitude);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"output",       VIVID_PORT_AUDIO, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"freq_cv",      VIVID_PORT_SIGNAL, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        out.push_back({"mod_index_cv", VIVID_PORT_SIGNAL, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        out.push_back({"gate_cv",      VIVID_PORT_SIGNAL, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
    }

    void process_audio(const VividAudioContext* ctx) override {
        float* out = ctx->output_buffers[0];
        uint32_t frames = ctx->buffer_size;

        float freq_cv      = ctx->input_float_values ? ctx->input_float_values[0] : 0.0f;
        float mod_index_cv = ctx->input_float_values ? ctx->input_float_values[1] : 0.0f;
        float gate_cv      = ctx->input_float_values ? ctx->input_float_values[2] : 0.0f;

        float freq = carrier_freq.value * std::pow(2.0f, freq_cv / 12.0f);
        if (freq < 20.0f)    freq = 20.0f;
        if (freq > 20000.0f) freq = 20000.0f;

        float mi = mod_index.value + mod_index_cv;
        if (mi < 0.0f) mi = 0.0f;
        if (mi > 20.0f) mi = 20.0f;

        float mod_freq = freq * mod_ratio.value;
        float amp = amplitude.value;
        double inv_sr = 1.0 / static_cast<double>(ctx->sample_rate);
        float dt = static_cast<float>(inv_sr);

        // Gate edge detection
        bool gate_on = gate_cv > 0.5f;
        bool prev_on = prev_gate_ > 0.5f;
        if (gate_on && !prev_on) vivid::adsr::gate_on(env_state_);
        if (!gate_on && prev_on) vivid::adsr::gate_off(env_state_);
        prev_gate_ = gate_cv;

        for (uint32_t i = 0; i < frames; i++) {
            vivid::adsr::advance(env_state_, dt, attack.value, decay.value,
                                  sustain.value, release.value);

            float mod_signal = mi * std::sin(2.0 * M_PI * mod_phase_);
            float sample = std::sin(2.0 * M_PI * carrier_phase_ + mod_signal);
            out[i] = sample * env_state_.env_value * amp;

            carrier_phase_ += static_cast<double>(freq) * inv_sr;
            if (carrier_phase_ >= 1.0) carrier_phase_ -= 1.0;

            mod_phase_ += static_cast<double>(mod_freq) * inv_sr;
            if (mod_phase_ >= 1.0) mod_phase_ -= 1.0;
        }
    }
};

VIVID_REGISTER(FmSynth)
