#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "../glitch_common/glitch_dsp.h"

#include <cmath>

// ---------------------------------------------------------------------------
// Scratch: DJ-style varispeed scratch with direction control
//
// On trigger: captures a buffer region and plays it back at variable speed
// with configurable motion patterns (back-forth, forward, backward, random).
// ---------------------------------------------------------------------------

struct Scratch : vivid::OperatorBase {
    static constexpr const char* kName   = "Scratch";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_AUDIO;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> phase     {"phase",      0.0f,   0.0f,   1.0f};
    vivid::Param<float> chance    {"chance",     0.4f,   0.0f,   1.0f};
    vivid::Param<float> size      {"size",       0.3f,   0.05f,  2.0f};
    vivid::Param<float> speed     {"speed",      1.0f,   0.125f, 4.0f};
    vivid::Param<float> speed_rand{"speed_rand", 0.3f,   0.0f,   1.0f};
    vivid::Param<int>   motion    {"motion",     0, {"BackForth","Forward","Backward","Random"}};
    vivid::Param<float> mix       {"mix",        1.0f,   0.0f,   1.0f};

    glitch::CircularBuffer buf_;
    glitch::WhiteNoise     rng_;
    float prev_phase_ = 0.0f;

    enum State { Passthrough, Scratching };
    State    state_        = Passthrough;
    uint32_t region_start_ = 0;
    uint32_t region_len_   = 0;
    uint32_t total_len_    = 0; // total scratch duration in samples
    uint32_t total_pos_    = 0;
    double   read_pos_     = 0.0;
    float    direction_    = 1.0f;
    float    cur_speed_    = 1.0f;

    static constexpr float kSpeedPool[] = {0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f};
    static constexpr int   kSpeedPoolSize = 6;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&phase);
        out.push_back(&chance);
        out.push_back(&size);
        out.push_back(&speed);
        out.push_back(&speed_rand);
        out.push_back(&motion);
        out.push_back(&mix);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"output", VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_OUTPUT});
    }

    float pick_speed() {
        float base = speed.value;
        float rand_amt = speed_rand.value;
        if (rand_amt > 0.0f) {
            int idx = static_cast<int>(rng_.next_unipolar() * kSpeedPoolSize);
            if (idx >= kSpeedPoolSize) idx = kSpeedPoolSize - 1;
            float pool_speed = kSpeedPool[idx];
            return base * (1.0f - rand_amt) + pool_speed * rand_amt;
        }
        return base;
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
        int   cur_motion = motion.int_value();
        uint32_t scratch_samples = static_cast<uint32_t>(size.value * audio->sample_rate);
        if (scratch_samples < 1) scratch_samples = 1;

        if (glitch::detect_trigger(cur_phase, prev_phase_) && state_ == Passthrough) {
            if (rng_.next_unipolar() < chance.value) {
                state_        = Scratching;
                // Capture 2x region for back-forth room
                region_len_   = scratch_samples * 2;
                if (region_len_ >= buf_.size) region_len_ = buf_.size - 1;
                region_start_ = buf_.get_read_pos(region_len_);
                total_len_    = scratch_samples;
                total_pos_    = 0;
                read_pos_     = 0.0;
                cur_speed_    = pick_speed();
                direction_    = (cur_motion == 2) ? -1.0f : 1.0f; // Backward starts reversed
            }
        }

        for (uint32_t i = 0; i < frames; i++) {
            buf_.write(in[i]);

            if (state_ == Scratching) {
                // Motion patterns
                if (cur_motion == 0) {
                    // BackForth: triangular direction oscillation
                    float half = static_cast<float>(total_len_) * 0.5f;
                    direction_ = (total_pos_ < static_cast<uint32_t>(half)) ? 1.0f : -1.0f;
                } else if (cur_motion == 3) {
                    // Random: 2% per-sample chance of direction flip + speed re-roll
                    if (rng_.next_unipolar() < 0.02f) {
                        direction_ = -direction_;
                        cur_speed_ = pick_speed();
                    }
                }

                // Read from buffer
                double abs_pos = static_cast<double>(region_start_) + read_pos_;
                float sample = buf_.read(abs_pos);

                // Crossfade
                float cf = glitch::crossfade_coeff(total_pos_, total_len_, 128);
                sample *= cf;

                out[i] = in[i] * dry + sample * wet;

                // Advance read position
                read_pos_ += direction_ * cur_speed_;

                // Clamp to captured region
                if (read_pos_ < 0.0) read_pos_ = 0.0;
                if (read_pos_ >= static_cast<double>(region_len_))
                    read_pos_ = static_cast<double>(region_len_) - 1.0;

                total_pos_++;
                if (total_pos_ >= total_len_) {
                    state_ = Passthrough;
                }
            } else {
                out[i] = in[i];
            }
        }

        prev_phase_ = cur_phase;
    }
};

constexpr float Scratch::kSpeedPool[];

VIVID_REGISTER(Scratch)
