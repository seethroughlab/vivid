#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "../drum_common/drum_dsp.h"

// ---------------------------------------------------------------------------
// DrumClap: 4 staggered noise bursts through SVF bandpass (stereo)
//
// Each burst has randomized timing (controlled by sloppy) and stereo panning.
// Filtered tail provides the reverberant decay.
// ---------------------------------------------------------------------------

struct DrumClap : vivid::OperatorBase {
    static constexpr const char* kName   = "DrumClap";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_AUDIO;
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> phase        {"phase",        0.0f,    0.0f,    1.0f};
    vivid::Param<float> decay        {"decay",        0.2f,    0.05f,   1.0f};
    vivid::Param<float> tone         {"tone",         0.5f,    0.0f,    1.0f};
    vivid::Param<float> sloppy       {"sloppy",       0.3f,    0.0f,    1.0f};
    vivid::Param<float> tail         {"tail",         0.3f,    0.0f,    1.0f};
    vivid::Param<float> stereo_width {"stereo_width", 0.5f,    0.0f,    1.0f};
    vivid::Param<float> tune         {"tune",      1500.0f,  500.0f, 5000.0f};
    vivid::Param<float> volume       {"volume",       0.7f,    0.0f,    1.0f};

    static constexpr int kNumBursts = 4;
    // Base burst offsets in seconds (~15ms apart)
    static constexpr double kBurstBase[kNumBursts] = {0.0, 0.015, 0.030, 0.045};

    drum::DecayEnvelope env_;
    drum::WhiteNoise    noise_;
    drum::SVF           filter_l_;
    drum::SVF           filter_r_;
    float               prev_phase_ = 0.0f;

    // Per-trigger randomized burst timing offsets and pan positions
    double burst_offsets_[kNumBursts] = {};
    float  burst_pan_[kNumBursts]     = {};  // -1 to 1

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&phase);
        out.push_back(&decay);
        out.push_back(&tone);
        out.push_back(&sloppy);
        out.push_back(&tail);
        out.push_back(&stereo_width);
        out.push_back(&tune);
        out.push_back(&volume);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"output_left",  VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_OUTPUT});
        out.push_back({"output_right", VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_OUTPUT});
    }

    void randomize_bursts(float slop, float width) {
        for (int b = 0; b < kNumBursts; b++) {
            // Randomize timing: base + random offset scaled by sloppy
            float r1 = noise_.next();
            burst_offsets_[b] = kBurstBase[b] + r1 * slop * 0.01; // up to 10ms variation

            // Randomize stereo pan
            float r2 = noise_.next();
            burst_pan_[b] = r2 * width;
        }
    }

    void process(const VividProcessContext* ctx) override {
        auto* audio = vivid_audio(ctx);
        if (!audio) return;

        float* out_l = audio->output_buffers[0];
        float* out_r = audio->output_buffers[1];
        double sr    = audio->sample_rate;
        double inv_sr = 1.0 / sr;

        float dec       = decay.value;
        float tn        = tone.value;
        float slop      = sloppy.value;
        float tl        = tail.value;
        float width     = stereo_width.value;
        float center    = tune.value;
        float vol       = volume.value;
        float cur_phase = phase.value;

        float cutoff = center + tn * 2000.0f;

        for (uint32_t i = 0; i < audio->buffer_size; i++) {
            if (i == 0 && drum::detect_trigger(cur_phase, prev_phase_)) {
                env_.trigger();
                randomize_bursts(slop, width);
            }

            float env = env_.value(dec);
            double t  = env_.time;

            // Sum burst contributions
            float burst_l = 0.0f;
            float burst_r = 0.0f;
            static constexpr double kBurstDuration = 0.008; // 8ms per burst

            for (int b = 0; b < kNumBursts; b++) {
                double burst_start = burst_offsets_[b];
                double burst_end   = burst_start + kBurstDuration;

                if (t >= burst_start && t < burst_end) {
                    float burst_env = 1.0f - static_cast<float>((t - burst_start) / kBurstDuration);
                    float n = noise_.next() * burst_env;

                    // Stereo pan
                    float pan = burst_pan_[b];
                    float gain_l = 0.5f * (1.0f - pan);
                    float gain_r = 0.5f * (1.0f + pan);
                    burst_l += n * gain_l;
                    burst_r += n * gain_r;
                }
            }

            // Tail: filtered noise with slow decay
            float tail_sample = 0.0f;
            if (tl > 0.0f) {
                tail_sample = noise_.next() * env * tl;
            }

            // Filter left and right
            float filt_l = filter_l_.process(burst_l + tail_sample * 0.5f, cutoff, 0.4f,
                                              static_cast<float>(sr), drum::SVF::BP);
            float filt_r = filter_r_.process(burst_r + tail_sample * 0.5f, cutoff, 0.4f,
                                              static_cast<float>(sr), drum::SVF::BP);

            out_l[i] = filt_l * env * vol;
            out_r[i] = filt_r * env * vol;

            env_.advance(inv_sr);
        }

        prev_phase_ = cur_phase;
    }
};

VIVID_REGISTER(DrumClap)
