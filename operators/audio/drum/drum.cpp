#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct Drum : vivid::OperatorBase {
    static constexpr const char* kName   = "Drum";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_AUDIO;
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> phase    {"phase",     0.0f, 0.0f, 1.0f};
    vivid::Param<float> frequency{"frequency", 80.0f, 20.0f, 500.0f};
    vivid::Param<float> decay    {"decay",     0.2f, 0.01f, 2.0f};
    vivid::Param<float> noise_mix{"noise",     0.3f, 0.0f, 1.0f};
    vivid::Param<float> pitch_env{"pitch_env", 2.0f, 1.0f, 8.0f};
    vivid::Param<float> amplitude{"amplitude", 0.5f, 0.0f, 1.0f};

    double tone_phase_    = 0.0;
    double trigger_time_  = 1000.0;  // large = silence at startup
    float  prev_phase_    = 0.0f;
    uint32_t rng_state_   = 12345;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&phase);
        out.push_back(&frequency);
        out.push_back(&decay);
        out.push_back(&noise_mix);
        out.push_back(&pitch_env);
        out.push_back(&amplitude);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"output", VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_OUTPUT});
    }

    // Simple fast PRNG for noise
    float next_noise() {
        rng_state_ = rng_state_ * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<int32_t>(rng_state_)) / 2147483648.0f;
    }

    void process(const VividProcessContext* ctx) override {
        auto* audio = vivid_audio(ctx);
        if (!audio) return;

        float* out = audio->output_buffers[0];
        double sample_rate = audio->sample_rate;
        double inv_sr = 1.0 / sample_rate;
        float cur_phase = phase.value;
        float freq = frequency.value;
        float dec = decay.value;
        float nmix = noise_mix.value;
        float penv = pitch_env.value;
        float amp = amplitude.value;

        for (uint32_t i = 0; i < audio->buffer_size; i++) {
            // Trigger detection: phase wraps (drops by > 0.5)
            // Interpolate trigger within buffer for sample accuracy
            float phase_at_sample = cur_phase;  // simplified: same across buffer
            if (i == 0) {
                float delta = cur_phase - prev_phase_;
                if (delta < -0.5f) {
                    trigger_time_ = 0.0;
                    tone_phase_ = 0.0;
                }
            }

            // Envelope: exponential decay
            double env = std::exp(-trigger_time_ * 5.0 / dec);

            // Pitch sweep: frequency starts at freq*pitch_env, decays to freq
            double pitch_mult = 1.0 + (penv - 1.0) * env;
            double cur_freq = freq * pitch_mult;

            // Tone: sine oscillator
            double tone = std::sin(tone_phase_ * 2.0 * M_PI);
            tone_phase_ += cur_freq * inv_sr;
            if (tone_phase_ >= 1.0) tone_phase_ -= 1.0;

            // Noise
            float noise_sample = next_noise();

            // Mix and output
            double mixed = tone * (1.0 - nmix) + noise_sample * nmix;
            out[i] = static_cast<float>(mixed * env) * amp;

            trigger_time_ += inv_sr;
        }

        prev_phase_ = cur_phase;
    }
};

VIVID_REGISTER(Drum)
