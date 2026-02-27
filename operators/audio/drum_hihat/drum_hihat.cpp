#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "../drum_common/drum_dsp.h"

// ---------------------------------------------------------------------------
// DrumHiHat: Filtered noise + 6 metallic ring oscillators
//
// Square waves at classic 808 hi-hat frequencies, SVF highpass filtered.
// Short decay = closed hat, long decay = open hat.
// ---------------------------------------------------------------------------

struct DrumHiHat : vivid::OperatorBase {
    static constexpr const char* kName   = "DrumHiHat";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_AUDIO;
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> phase  {"phase",   0.0f,  0.0f, 1.0f};
    vivid::Param<float> decay  {"decay",   0.08f, 0.01f, 2.0f};
    vivid::Param<float> tone   {"tone",    0.5f,  0.0f, 1.0f};
    vivid::Param<float> ring   {"ring",    0.5f,  0.0f, 1.0f};
    vivid::Param<float> pitch  {"pitch",   1.0f,  0.5f, 2.0f};
    vivid::Param<float> attack {"attack",  0.002f, 0.0f, 0.05f};
    vivid::Param<float> volume {"volume",  0.7f,  0.0f, 1.0f};

    // 808-style metallic ring frequencies
    static constexpr float kRingFreqs[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};

    drum::DecayEnvelope env_;
    drum::WhiteNoise    noise_;
    drum::SVF           hp_filter_;
    double              ring_phases_[6] = {};
    float               prev_phase_ = 0.0f;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&phase);
        out.push_back(&decay);
        out.push_back(&tone);
        out.push_back(&ring);
        out.push_back(&pitch);
        out.push_back(&attack);
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

        float dec      = decay.value;
        float tn       = tone.value;
        float rng      = ring.value;
        float p_mult   = pitch.value;
        float atk      = attack.value;
        float vol      = volume.value;
        float cur_phase = phase.value;

        float cutoff = 4000.0f + tn * 8000.0f;

        for (uint32_t i = 0; i < audio->buffer_size; i++) {
            if (i == 0 && drum::detect_trigger(cur_phase, prev_phase_)) {
                env_.trigger();
                for (int r = 0; r < 6; r++) ring_phases_[r] = 0.0;
            }

            float env = env_.value(dec);

            // Attack shaping
            if (atk > 0.0f && env_.time < atk) {
                env *= static_cast<float>(env_.time / atk);
            }

            // Ring oscillators: square waves
            float ring_sum = audio_dsp::ring_osc_bank(ring_phases_, kRingFreqs, 6, p_mult, inv_sr);

            // Noise component
            float noise_sample = noise_.next();

            // Blend ring vs noise
            float raw = ring_sum * rng + noise_sample * (1.0f - rng);

            // Highpass filter
            float filtered = hp_filter_.process(raw, cutoff, 0.3f,
                                                 static_cast<float>(sr), drum::SVF::HP);

            out[i] = filtered * env * vol;

            env_.advance(inv_sr);
        }

        prev_phase_ = cur_phase;
    }
};

VIVID_REGISTER(DrumHiHat)
