#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "../drum_common/drum_dsp.h"

// ---------------------------------------------------------------------------
// DrumCymbal: 12 inharmonic ring oscillators + noise sizzle
//
// Square waves at inharmonically-related frequencies create metallic timbre.
// SVF highpass for brightness. Shimmer LFO amplitude modulation.
// Long decay times: 1-2s = ride, 3+ = crash.
// ---------------------------------------------------------------------------

struct DrumCymbal : vivid::OperatorBase {
    static constexpr const char* kName   = "DrumCymbal";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_AUDIO;
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> phase   {"phase",   0.0f,  0.0f,  1.0f};
    vivid::Param<float> decay   {"decay",   3.0f,  0.5f, 10.0f};
    vivid::Param<float> tone    {"tone",    0.5f,  0.0f,  1.0f};
    vivid::Param<float> pitch   {"pitch",   1.0f,  0.5f,  2.0f};
    vivid::Param<float> shimmer {"shimmer", 0.3f,  0.0f,  1.0f};
    vivid::Param<float> sizzle  {"sizzle",  0.3f,  0.0f,  1.0f};
    vivid::Param<float> volume  {"volume",  0.6f,  0.0f,  1.0f};

    // 12 inharmonic frequencies for metallic cymbal timbre
    static constexpr int kNumOsc = 12;
    static constexpr float kOscFreqs[kNumOsc] = {
        205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f,
        1043.f, 1185.f, 1380.f, 1672.f, 1960.f, 2418.f
    };

    drum::DecayEnvelope env_;
    drum::WhiteNoise    noise_;
    drum::SVF           hp_filter_;
    double              ring_phases_[kNumOsc] = {};
    double              lfo_phase_ = 0.0;
    float               prev_phase_ = 0.0f;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&phase);
        out.push_back(&decay);
        out.push_back(&tone);
        out.push_back(&pitch);
        out.push_back(&shimmer);
        out.push_back(&sizzle);
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
        float p_mult   = pitch.value;
        float shm      = shimmer.value;
        float szl      = sizzle.value;
        float vol      = volume.value;
        float cur_phase = phase.value;

        float cutoff = 3000.0f + tn * 9000.0f;

        for (uint32_t i = 0; i < audio->buffer_size; i++) {
            if (i == 0 && drum::detect_trigger(cur_phase, prev_phase_)) {
                env_.trigger();
                for (int r = 0; r < kNumOsc; r++) ring_phases_[r] = 0.0;
                lfo_phase_ = 0.0;
            }

            float env = env_.value(dec);

            // Ring oscillators: square waves
            float ring_sum = audio_dsp::ring_osc_bank(ring_phases_, kOscFreqs, kNumOsc, p_mult, inv_sr);

            // Noise sizzle
            float noise_sample = noise_.next() * szl;

            // Mix ring + noise
            float raw = ring_sum + noise_sample;

            // Highpass filter
            float filtered = hp_filter_.process(raw, cutoff, 0.2f,
                                                 static_cast<float>(sr), drum::SVF::HP);

            // Shimmer LFO: amplitude modulation
            float lfo_mod = 1.0f;
            if (shm > 0.0f) {
                double lfo_freq = 4.0 + shm * 8.0;
                float lfo = static_cast<float>(std::sin(lfo_phase_ * 2.0 * M_PI));
                lfo_mod = 1.0f - shm * 0.5f * (1.0f - lfo); // modulation depth
                lfo_phase_ += lfo_freq * inv_sr;
                if (lfo_phase_ >= 1.0) lfo_phase_ -= 1.0;
            }

            out[i] = filtered * env * lfo_mod * vol;

            env_.advance(inv_sr);
        }

        prev_phase_ = cur_phase;
    }
};

VIVID_REGISTER(DrumCymbal)
