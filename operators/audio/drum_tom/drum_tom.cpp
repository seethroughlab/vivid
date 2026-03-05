#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "operator_api/drum_dsp.h"

// ---------------------------------------------------------------------------
// DrumTom: Pitched sine + harmonics with pitch bend envelope
//
// SVF bandpass for body resonance. Pitch drops from (pitch * (1+bend))
// down to base pitch over bend_time.
// ---------------------------------------------------------------------------

struct DrumTom : vivid::OperatorBase {
    static constexpr const char* kName   = "DrumTom";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_AUDIO;
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> phase     {"phase",     0.0f,   0.0f,   1.0f};
    vivid::Param<float> pitch     {"pitch",   120.0f,  40.0f, 400.0f};
    vivid::Param<float> bend      {"bend",      0.3f,   0.0f,   1.0f};
    vivid::Param<float> bend_time {"bend_time", 0.08f,  0.01f,  0.3f};
    vivid::Param<float> color     {"color",     0.3f,   0.0f,   1.0f};
    vivid::Param<float> tone      {"tone",      0.5f,   0.0f,   1.0f};
    vivid::Param<float> decay     {"decay",     0.3f,   0.1f,   2.0f};
    vivid::Param<float> volume    {"volume",    0.8f,   0.0f,   1.0f};

    drum::DecayEnvelope amp_env_;
    drum::DecayEnvelope bend_env_;
    drum::SVF           body_filter_;
    double              osc_phase_ = 0.0;
    float               prev_phase_ = 0.0f;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&phase);
        out.push_back(&pitch);
        out.push_back(&bend);
        out.push_back(&bend_time);
        out.push_back(&color);
        out.push_back(&tone);
        out.push_back(&decay);
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

        float p_base   = pitch.value;
        float bnd      = bend.value;
        float bnd_time = bend_time.value;
        float col      = color.value;
        float tn       = tone.value;
        float dec      = decay.value;
        float vol      = volume.value;
        float cur_phase = phase.value;

        // Body filter: bandpass centered on pitch, amount controlled by tone
        float filter_cutoff = p_base * (1.0f + tn * 2.0f);
        float filter_reso   = 0.2f + tn * 0.4f;

        for (uint32_t i = 0; i < audio->buffer_size; i++) {
            if (i == 0 && drum::detect_trigger(cur_phase, prev_phase_)) {
                amp_env_.trigger();
                bend_env_.trigger();
                osc_phase_ = 0.0;
            }

            float amp  = amp_env_.value(dec);
            float benv = bend_env_.value(bnd_time);

            // Pitch bend: starts high, drops to base
            double freq = p_base * (1.0 + bnd * benv);

            // Sine + harmonics
            double body = audio_dsp::harmonics_3(osc_phase_, col);

            // Bandpass filter for body resonance
            float sample = static_cast<float>(body) * amp;
            if (tn > 0.0f) {
                sample = body_filter_.process(sample, filter_cutoff, filter_reso,
                                               static_cast<float>(sr), drum::SVF::BP);
            }

            out[i] = sample * vol;

            osc_phase_ += freq * inv_sr;
            if (osc_phase_ >= 1.0) osc_phase_ -= 1.0;
            amp_env_.advance(inv_sr);
            bend_env_.advance(inv_sr);
        }

        prev_phase_ = cur_phase;
    }
};

VIVID_REGISTER(DrumTom)
