#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "../drum_common/drum_dsp.h"

// ---------------------------------------------------------------------------
// DrumSnare: Sine body + harmonics + SVF-filtered white noise
//
// Independent tone/noise decay times for snappy control.
// SVF bandpass on noise controlled by snappy param.
// ---------------------------------------------------------------------------

struct DrumSnare : vivid::OperatorBase {
    static constexpr const char* kName   = "DrumSnare";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_AUDIO;
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> phase      {"phase",       0.0f,   0.0f,  1.0f};
    vivid::Param<float> tone_level {"tone",        0.5f,   0.0f,  1.0f};
    vivid::Param<float> noise_level{"noise",       0.5f,   0.0f,  1.0f};
    vivid::Param<float> pitch      {"pitch",     200.0f, 100.0f, 400.0f};
    vivid::Param<float> tone_decay {"tone_decay",  0.1f,  0.01f,  0.5f};
    vivid::Param<float> noise_decay{"noise_decay", 0.2f,  0.05f,  0.5f};
    vivid::Param<float> snappy     {"snappy",      0.5f,   0.0f,  1.0f};
    vivid::Param<float> color      {"color",       0.5f,   0.0f,  1.0f};
    vivid::Param<float> volume     {"volume",      0.8f,   0.0f,  1.0f};

    drum::DecayEnvelope tone_env_;
    drum::DecayEnvelope noise_env_;
    drum::WhiteNoise    noise_;
    drum::SVF           noise_filter_;
    double              osc_phase_ = 0.0;
    float               prev_phase_ = 0.0f;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&phase);
        out.push_back(&tone_level);
        out.push_back(&noise_level);
        out.push_back(&pitch);
        out.push_back(&tone_decay);
        out.push_back(&noise_decay);
        out.push_back(&snappy);
        out.push_back(&color);
        out.push_back(&volume);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"output", VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        auto* audio = vivid_audio(ctx);
        if (!audio) return;

        float* out = audio->output_buffers[0];
        double sr  = audio->sample_rate;
        double inv_sr = 1.0 / sr;

        float p       = pitch.value;
        float t_level = tone_level.value;
        float n_level = noise_level.value;
        float t_dec   = tone_decay.value;
        float n_dec   = noise_decay.value;
        float snap    = snappy.value;
        float col     = color.value;
        float vol     = volume.value;
        float cur_phase = phase.value;

        float cutoff = 2000.0f + snap * 4000.0f;

        for (uint32_t i = 0; i < audio->buffer_size; i++) {
            if (i == 0 && drum::detect_trigger(cur_phase, prev_phase_)) {
                tone_env_.trigger();
                noise_env_.trigger();
                osc_phase_ = 0.0;
            }

            float t_env = tone_env_.value(t_dec);
            float n_env = noise_env_.value(n_dec);

            // Tone body: sine + harmonics controlled by color
            double body = audio_dsp::harmonics_3(osc_phase_, col);

            // Filtered noise
            float raw_noise = noise_.next();
            float filt_noise = noise_filter_.process(raw_noise, cutoff, 0.3f,
                                                      static_cast<float>(sr), drum::SVF::BP);

            // Mix
            float sample = static_cast<float>(body) * t_env * t_level
                         + filt_noise * n_env * n_level;

            out[i] = sample * vol;

            osc_phase_ += p * inv_sr;
            if (osc_phase_ >= 1.0) osc_phase_ -= 1.0;
            tone_env_.advance(inv_sr);
            noise_env_.advance(inv_sr);
        }

        prev_phase_ = cur_phase;
    }
};

VIVID_REGISTER(DrumSnare)
