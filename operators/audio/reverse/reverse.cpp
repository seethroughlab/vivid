#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "../glitch_common/glitch_dsp.h"

// ---------------------------------------------------------------------------
// Reverse: beat-synced reverse playback
//
// On trigger: captures a slice and plays it backwards with 128-sample
// crossfade at start/end for click-free transitions.
// ---------------------------------------------------------------------------

struct Reverse : vivid::OperatorBase {
    static constexpr const char* kName   = "Reverse";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_AUDIO;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> phase {"phase",  0.0f,  0.0f, 1.0f};
    vivid::Param<float> chance{"chance", 0.5f,  0.0f, 1.0f};
    vivid::Param<float> size  {"size",   0.25f, 0.05f, 2.0f};
    vivid::Param<float> mix   {"mix",    1.0f,  0.0f, 1.0f};

    glitch::CircularBuffer buf_;
    glitch::WhiteNoise     rng_;
    float prev_phase_ = 0.0f;

    enum State { Passthrough, Reversing };
    State    state_       = Passthrough;
    uint32_t slice_end_   = 0; // end of captured slice (where we start reading backward)
    uint32_t slice_len_   = 0;
    uint32_t slice_pos_   = 0;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&phase);
        out.push_back(&chance);
        out.push_back(&size);
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
        uint32_t slice_samples = static_cast<uint32_t>(size.value * audio->sample_rate);
        if (slice_samples < 1) slice_samples = 1;

        if (glitch::detect_trigger(cur_phase, prev_phase_) && state_ == Passthrough) {
            if (rng_.next_unipolar() < chance.value) {
                state_     = Reversing;
                slice_len_ = slice_samples;
                // slice_end_ is the most recent sample (1 frame before write head)
                slice_end_ = buf_.get_read_pos(1);
                slice_pos_ = 0;
            }
        }

        for (uint32_t i = 0; i < frames; i++) {
            buf_.write(in[i]);

            if (state_ == Reversing) {
                // Read backwards from slice_end_
                float sample = buf_.read_reverse(slice_end_, static_cast<double>(slice_pos_));

                // 128-sample crossfade at edges
                float cf = glitch::crossfade_coeff(slice_pos_, slice_len_, 128);
                sample *= cf;

                out[i] = in[i] * dry + sample * wet;

                slice_pos_++;
                if (slice_pos_ >= slice_len_) {
                    state_ = Passthrough;
                }
            } else {
                out[i] = in[i];
            }
        }

        prev_phase_ = cur_phase;
    }
};

VIVID_REGISTER(Reverse)
