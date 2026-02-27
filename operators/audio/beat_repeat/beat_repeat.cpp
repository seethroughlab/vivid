#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "../glitch_common/glitch_dsp.h"

// ---------------------------------------------------------------------------
// BeatRepeat: beat-synced slice repeater with per-repeat volume decay
//
// Simpler than Stutter — captures a slice on trigger, replays it `count`
// times with each repetition multiplied by (1-decay)^n.
// ---------------------------------------------------------------------------

struct BeatRepeat : vivid::OperatorBase {
    static constexpr const char* kName   = "BeatRepeat";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_AUDIO;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> phase {"phase",  0.0f,  0.0f,  1.0f};
    vivid::Param<float> chance{"chance", 0.5f,  0.0f,  1.0f};
    vivid::Param<float> size  {"size",   0.15f, 0.02f, 1.0f};
    vivid::Param<int>   count {"count",  4,     1,     16};
    vivid::Param<float> decay {"decay",  0.1f,  0.0f,  1.0f};
    vivid::Param<float> mix   {"mix",    1.0f,  0.0f,  1.0f};

    glitch::CircularBuffer buf_;
    glitch::WhiteNoise     rng_;
    float prev_phase_ = 0.0f;

    enum State { Passthrough, Repeating };
    State    state_       = Passthrough;
    uint32_t slice_start_ = 0;
    uint32_t slice_len_   = 0;
    uint32_t slice_pos_   = 0;
    int      current_rep_ = 0;
    int      total_reps_  = 0;
    float    rep_gain_    = 1.0f;
    float    decay_factor_= 1.0f;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&phase);
        out.push_back(&chance);
        out.push_back(&size);
        out.push_back(&count);
        out.push_back(&decay);
        out.push_back(&mix);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"output", VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        auto* audio = vivid_audio(ctx);
        if (!audio) return;

        buf_.init(audio->sample_rate);

        float* in  = audio->input_buffers[0];
        float* out = audio->output_buffers[0];
        uint32_t frames = audio->buffer_size;

        float cur_phase = phase.value;
        float wet = mix.value;
        float dry = 1.0f - wet;
        int   cnt = count.int_value();
        uint32_t slice_samples = static_cast<uint32_t>(size.value * audio->sample_rate);
        if (slice_samples < 1) slice_samples = 1;

        if (glitch::detect_trigger(cur_phase, prev_phase_) && state_ == Passthrough) {
            if (rng_.next_unipolar() < chance.value) {
                state_        = Repeating;
                slice_len_    = slice_samples;
                slice_start_  = buf_.get_read_pos(slice_len_);
                slice_pos_    = 0;
                current_rep_  = 0;
                total_reps_   = cnt;
                rep_gain_     = 1.0f;
                decay_factor_ = 1.0f - decay.value;
            }
        }

        for (uint32_t i = 0; i < frames; i++) {
            buf_.write(in[i]);

            if (state_ == Repeating) {
                double read_pos = static_cast<double>(slice_start_) + slice_pos_;
                float sample = buf_.read(read_pos);

                float cf = glitch::crossfade_coeff(slice_pos_, slice_len_, 64);
                sample *= cf * rep_gain_;

                out[i] = in[i] * dry + sample * wet;

                slice_pos_++;
                if (slice_pos_ >= slice_len_) {
                    slice_pos_ = 0;
                    current_rep_++;
                    rep_gain_ *= decay_factor_;
                    if (current_rep_ >= total_reps_) {
                        state_ = Passthrough;
                    }
                }
            } else {
                out[i] = in[i];
            }
        }

        prev_phase_ = cur_phase;
    }
};

VIVID_REGISTER(BeatRepeat)
