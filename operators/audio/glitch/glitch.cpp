#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "../glitch_common/glitch_dsp.h"

#include <cmath>

// ---------------------------------------------------------------------------
// Glitch: meta-effect — random effect selector
//
// On phase wrap: selects one effect from weighted probability sliders.
// Contains simplified inline implementations of each sub-effect.
// Runs selected effect to completion before allowing next trigger.
// ---------------------------------------------------------------------------

struct Glitch : vivid::OperatorBase {
    static constexpr const char* kName   = "Glitch";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_AUDIO;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> phase         {"phase",          0.0f, 0.0f, 1.0f};
    vivid::Param<float> repeat_chance {"repeat_chance",  0.2f, 0.0f, 1.0f};
    vivid::Param<float> reverse_chance{"reverse_chance", 0.15f, 0.0f, 1.0f};
    vivid::Param<float> stutter_chance{"stutter_chance", 0.15f, 0.0f, 1.0f};
    vivid::Param<float> scratch_chance{"scratch_chance", 0.1f, 0.0f, 1.0f};
    vivid::Param<float> tape_chance   {"tape_chance",    0.08f, 0.0f, 1.0f};
    vivid::Param<float> shift_chance  {"shift_chance",   0.1f, 0.0f, 1.0f};
    vivid::Param<float> mix           {"mix",            1.0f, 0.0f, 1.0f};

    glitch::CircularBuffer buf_;
    glitch::WhiteNoise     rng_;
    float prev_phase_ = 0.0f;

    enum Effect { None, Repeat, Reverse, Stutter, Scratch, TapeStop, FreqShift };
    Effect   active_effect_ = None;
    uint32_t effect_pos_    = 0;
    uint32_t effect_len_    = 0;

    // Shared effect state
    uint32_t slice_start_  = 0;
    uint32_t slice_len_    = 0;
    uint32_t slice_pos_    = 0;
    int      rep_count_    = 0;
    int      rep_current_  = 0;
    float    rep_gain_     = 1.0f;
    float    direction_    = 1.0f;
    double   read_pos_     = 0.0;
    float    speed_        = 1.0f;
    double   tape_offset_  = 0.0;

    // Freq shift state (simplified: 4-stage allpass approximation)
    float allpass_x_[4] = {};
    float allpass_y_[4] = {};
    double osc_phase_   = 0.0;
    float shift_hz_     = 0.0f;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&phase);
        out.push_back(&repeat_chance);
        out.push_back(&reverse_chance);
        out.push_back(&stutter_chance);
        out.push_back(&scratch_chance);
        out.push_back(&tape_chance);
        out.push_back(&shift_chance);
        out.push_back(&mix);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"output", VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_OUTPUT});
    }

    Effect select_effect() {
        float chances[] = {
            repeat_chance.value,
            reverse_chance.value,
            stutter_chance.value,
            scratch_chance.value,
            tape_chance.value,
            shift_chance.value
        };
        float total = 0.0f;
        for (int i = 0; i < 6; i++) total += chances[i];
        if (total <= 0.0f) return None;

        float roll = rng_.next_unipolar() * total;
        float cumulative = 0.0f;
        Effect effects[] = { Repeat, Reverse, Stutter, Scratch, TapeStop, FreqShift };
        for (int i = 0; i < 6; i++) {
            cumulative += chances[i];
            if (roll < cumulative) return effects[i];
        }
        return effects[5];
    }

    void start_effect(Effect e, uint32_t sr) {
        active_effect_ = e;
        effect_pos_ = 0;

        switch (e) {
            case Repeat: {
                float sz = 0.1f + rng_.next_unipolar() * 0.15f;
                slice_len_   = static_cast<uint32_t>(sz * sr);
                slice_start_ = buf_.get_read_pos(slice_len_);
                slice_pos_   = 0;
                rep_count_   = 3 + static_cast<int>(rng_.next_unipolar() * 5);
                rep_current_ = 0;
                rep_gain_    = 1.0f;
                effect_len_  = slice_len_ * rep_count_;
                break;
            }
            case Reverse: {
                float sz = 0.15f + rng_.next_unipolar() * 0.25f;
                slice_len_   = static_cast<uint32_t>(sz * sr);
                slice_start_ = buf_.get_read_pos(1);
                slice_pos_   = 0;
                effect_len_  = slice_len_;
                break;
            }
            case Stutter: {
                float sz = 0.04f + rng_.next_unipolar() * 0.08f;
                slice_len_   = static_cast<uint32_t>(sz * sr);
                slice_start_ = buf_.get_read_pos(slice_len_);
                slice_pos_   = 0;
                rep_count_   = 4 + static_cast<int>(rng_.next_unipolar() * 12);
                rep_current_ = 0;
                effect_len_  = slice_len_ * rep_count_;
                break;
            }
            case Scratch: {
                float sz = 0.2f + rng_.next_unipolar() * 0.3f;
                uint32_t region = static_cast<uint32_t>(sz * sr * 2);
                if (region >= buf_.size) region = buf_.size - 1;
                slice_start_ = buf_.get_read_pos(region);
                slice_len_   = region;
                effect_len_  = static_cast<uint32_t>(sz * sr);
                read_pos_    = 0.0;
                direction_   = 1.0f;
                speed_       = 0.5f + rng_.next_unipolar() * 1.5f;
                break;
            }
            case TapeStop: {
                float stop_t = 0.3f + rng_.next_unipolar() * 0.5f;
                effect_len_  = static_cast<uint32_t>(stop_t * sr);
                tape_offset_ = 0.0;
                break;
            }
            case FreqShift: {
                float dur = 0.2f + rng_.next_unipolar() * 0.4f;
                effect_len_ = static_cast<uint32_t>(dur * sr);
                shift_hz_ = (rng_.next() > 0.0f ? 1.0f : -1.0f) * (10.0f + rng_.next_unipolar() * 90.0f);
                osc_phase_ = 0.0;
                for (int j = 0; j < 4; j++) { allpass_x_[j] = 0.0f; allpass_y_[j] = 0.0f; }
                break;
            }
            default:
                break;
        }
    }

    float process_effect(float in_sample, uint32_t sr) {
        double inv_sr = 1.0 / static_cast<double>(sr);

        switch (active_effect_) {
            case Repeat: {
                double rp = static_cast<double>(slice_start_) + slice_pos_;
                float s = buf_.read(rp);
                float cf = glitch::crossfade_coeff(slice_pos_, slice_len_, 64);
                s *= cf * rep_gain_;
                slice_pos_++;
                if (slice_pos_ >= slice_len_) {
                    slice_pos_ = 0;
                    rep_current_++;
                    rep_gain_ *= 0.9f;
                }
                return s;
            }
            case Reverse: {
                float s = buf_.read_reverse(slice_start_, static_cast<double>(slice_pos_));
                float cf = glitch::crossfade_coeff(slice_pos_, slice_len_, 128);
                s *= cf;
                slice_pos_++;
                return s;
            }
            case Stutter: {
                double rp = static_cast<double>(slice_start_) + slice_pos_;
                float s = buf_.read(rp);
                float cf = glitch::crossfade_coeff(slice_pos_, slice_len_, 64);
                float progress = static_cast<float>(rep_current_) /
                                 static_cast<float>(rep_count_ > 1 ? rep_count_ - 1 : 1);
                float env = 1.0f - progress * 0.7f;
                s *= cf * env;
                slice_pos_++;
                if (slice_pos_ >= slice_len_) {
                    slice_pos_ = 0;
                    rep_current_++;
                }
                return s;
            }
            case Scratch: {
                // Back-forth direction
                float half = static_cast<float>(effect_len_) * 0.5f;
                direction_ = (effect_pos_ < static_cast<uint32_t>(half)) ? 1.0f : -1.0f;

                double abs_pos = static_cast<double>(slice_start_) + read_pos_;
                float s = buf_.read(abs_pos);
                float cf = glitch::crossfade_coeff(effect_pos_, effect_len_, 128);
                s *= cf;

                read_pos_ += direction_ * speed_;
                if (read_pos_ < 0.0) read_pos_ = 0.0;
                if (read_pos_ >= static_cast<double>(slice_len_))
                    read_pos_ = static_cast<double>(slice_len_) - 1.0;
                return s;
            }
            case TapeStop: {
                float progress = static_cast<float>(effect_pos_) / static_cast<float>(effect_len_);
                float rate = (1.0f - progress) * (1.0f - progress) * (1.0f - progress);
                tape_offset_ += rate;
                uint32_t fb = static_cast<uint32_t>(effect_len_ - tape_offset_);
                if (fb >= buf_.size) fb = buf_.size - 1;
                return buf_.read(static_cast<double>(buf_.get_read_pos(fb)));
            }
            case FreqShift: {
                // Simplified freq shift: 4-stage allpass Hilbert approximation
                // Allpass coefficients for ~90 degree phase split
                static const float a_coeffs[4] = {0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f};
                float x = in_sample;
                float q = x;
                for (int j = 0; j < 4; j++) {
                    float y = a_coeffs[j] * (x - allpass_y_[j]) + allpass_x_[j];
                    allpass_x_[j] = x;
                    allpass_y_[j] = y;
                    if (j == 1) q = y; // branch after 2nd stage for Q
                    x = y;
                }
                float i_sig = x; // I after all 4 stages

                float cos_osc = static_cast<float>(std::cos(osc_phase_ * 2.0 * M_PI));
                float sin_osc = static_cast<float>(std::sin(osc_phase_ * 2.0 * M_PI));
                float shifted = i_sig * cos_osc - q * sin_osc;

                osc_phase_ += shift_hz_ * inv_sr;
                osc_phase_ -= std::floor(osc_phase_);
                return shifted;
            }
            default:
                return in_sample;
        }
    }

    void process(const VividProcessContext* ctx) override {
        auto* audio = vivid_audio(ctx);
        if (!audio) return;

        buf_.init(audio->sample_rate);

        float* in  = audio->input_buffers[0];
        float* out = audio->output_buffers[0];
        uint32_t frames = audio->buffer_size;
        uint32_t sr = audio->sample_rate;

        float cur_phase = phase.value;
        float wet = mix.value;
        float dry = 1.0f - wet;

        if (glitch::detect_trigger(cur_phase, prev_phase_) && active_effect_ == None) {
            Effect e = select_effect();
            if (e != None) {
                start_effect(e, sr);
            }
        }

        for (uint32_t i = 0; i < frames; i++) {
            buf_.write(in[i]);

            if (active_effect_ != None) {
                float sample = process_effect(in[i], sr);
                out[i] = in[i] * dry + sample * wet;

                effect_pos_++;
                if (effect_pos_ >= effect_len_) {
                    active_effect_ = None;
                }
            } else {
                out[i] = in[i];
            }
        }

        prev_phase_ = cur_phase;
    }
};

VIVID_REGISTER(Glitch)
