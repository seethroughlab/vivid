#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "../glitch_common/glitch_dsp.h"

#include <cmath>

// ---------------------------------------------------------------------------
// Stretch: granular time-stretch (pitch preserved)
//
// On trigger: captures a source region and stretches it using overlapping
// Hann-windowed grains. Source phase advances at 1/factor rate so the
// pitch stays the same while duration changes.
// ---------------------------------------------------------------------------

struct Stretch : vivid::OperatorBase {
    static constexpr const char* kName   = "Stretch";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_AUDIO;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> phase     {"phase",      0.0f, 0.0f,  1.0f};
    vivid::Param<float> chance    {"chance",     0.3f, 0.0f,  1.0f};
    vivid::Param<float> size      {"size",       0.3f, 0.05f, 2.0f};
    vivid::Param<float> factor    {"factor",     2.0f, 0.25f, 4.0f};
    vivid::Param<float> grain_size{"grain_size", 0.05f, 0.01f, 0.2f};
    vivid::Param<float> grain_rand{"grain_rand", 0.1f,  0.0f,  0.5f};
    vivid::Param<float> overlap   {"overlap",    0.5f,  0.25f, 0.75f};
    vivid::Param<float> mix       {"mix",        1.0f,  0.0f,  1.0f};

    glitch::CircularBuffer buf_;
    glitch::WhiteNoise     rng_;
    float prev_phase_ = 0.0f;

    static constexpr int kMaxGrains = 8;

    struct Grain {
        bool     active    = false;
        uint32_t pos       = 0;   // position within grain
        uint32_t length    = 0;   // grain length in samples
        double   buf_start = 0.0; // where in the circular buffer this grain reads from
    };

    enum State { Passthrough, Stretching };
    State    state_            = Passthrough;
    uint32_t source_start_     = 0;
    uint32_t source_len_       = 0;
    uint32_t total_len_        = 0; // stretched output length
    uint32_t total_pos_        = 0;
    double   source_phase_     = 0.0; // progress through source (0..1)
    uint32_t next_grain_timer_ = 0;
    Grain    grains_[kMaxGrains];

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&phase);
        out.push_back(&chance);
        out.push_back(&size);
        out.push_back(&factor);
        out.push_back(&grain_size);
        out.push_back(&grain_rand);
        out.push_back(&overlap);
        out.push_back(&mix);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"output", VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_OUTPUT});
    }

    float hann_window(uint32_t pos, uint32_t length) const {
        if (length <= 1) return 1.0f;
        float t = static_cast<float>(pos) / static_cast<float>(length - 1);
        return 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * t));
    }

    void spawn_grain(uint32_t sr) {
        uint32_t g_len = static_cast<uint32_t>(grain_size.value * sr);
        if (g_len < 1) g_len = 1;

        // Source position with randomization
        double src_pos = source_phase_ * source_len_;
        float rand_offset = rng_.next() * grain_rand.value * static_cast<float>(g_len);
        src_pos += rand_offset;
        if (src_pos < 0.0) src_pos = 0.0;
        if (src_pos >= source_len_) src_pos = static_cast<double>(source_len_) - 1.0;

        double buf_pos = static_cast<double>(source_start_) + src_pos;

        for (int g = 0; g < kMaxGrains; g++) {
            if (!grains_[g].active) {
                grains_[g].active    = true;
                grains_[g].pos       = 0;
                grains_[g].length    = g_len;
                grains_[g].buf_start = buf_pos;
                break;
            }
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
        float fct = factor.value;

        uint32_t src_samples = static_cast<uint32_t>(size.value * sr);
        if (src_samples < 1) src_samples = 1;

        if (glitch::detect_trigger(cur_phase, prev_phase_) && state_ == Passthrough) {
            if (rng_.next_unipolar() < chance.value) {
                state_        = Stretching;
                source_len_   = src_samples;
                source_start_ = buf_.get_read_pos(source_len_);
                total_len_    = static_cast<uint32_t>(source_len_ * fct);
                if (total_len_ < 1) total_len_ = 1;
                total_pos_    = 0;
                source_phase_ = 0.0;
                next_grain_timer_ = 0;
                for (int g = 0; g < kMaxGrains; g++) grains_[g].active = false;
            }
        }

        // Grain spawn interval
        uint32_t g_len = static_cast<uint32_t>(grain_size.value * sr);
        if (g_len < 1) g_len = 1;
        uint32_t spawn_interval = static_cast<uint32_t>(g_len * (1.0f - overlap.value));
        if (spawn_interval < 1) spawn_interval = 1;

        // Expected overlapping grains for normalization
        float expected_overlap = static_cast<float>(g_len) / static_cast<float>(spawn_interval);
        float norm = 1.0f / (expected_overlap * 0.5f); // Hann window average is 0.5

        for (uint32_t i = 0; i < frames; i++) {
            buf_.write(in[i]);

            if (state_ == Stretching) {
                // Spawn grains
                if (next_grain_timer_ == 0) {
                    spawn_grain(sr);
                    next_grain_timer_ = spawn_interval;
                }
                next_grain_timer_--;

                // Sum active grains
                float sample = 0.0f;
                for (int g = 0; g < kMaxGrains; g++) {
                    if (!grains_[g].active) continue;
                    double read_pos = grains_[g].buf_start + grains_[g].pos;
                    float grain_sample = buf_.read(read_pos);
                    float window = hann_window(grains_[g].pos, grains_[g].length);
                    sample += grain_sample * window;

                    grains_[g].pos++;
                    if (grains_[g].pos >= grains_[g].length)
                        grains_[g].active = false;
                }

                sample *= norm;

                // Crossfade at effect boundaries
                float cf = glitch::crossfade_coeff(total_pos_, total_len_, 128);
                sample *= cf;

                out[i] = in[i] * dry + sample * wet;

                // Advance source phase
                source_phase_ += 1.0 / static_cast<double>(total_len_);
                if (source_phase_ > 1.0) source_phase_ = 1.0;

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

VIVID_REGISTER(Stretch)
