#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "../glitch_common/glitch_dsp.h"

// ---------------------------------------------------------------------------
// TapeStop: tape deck slowdown/speedup simulation
//
// State machine: Passthrough -> Stopping -> Stopped -> Starting -> Passthrough
// Stopping: cubic deceleration (rate = (1-progress)^3), pitch drops naturally.
// Starting: quadratic acceleration (rate = progress^2).
// Stopped: 100ms silence gap.
// ---------------------------------------------------------------------------

struct TapeStop : vivid::OperatorBase {
    static constexpr const char* kName   = "TapeStop";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_AUDIO;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> phase     {"phase",      0.0f,  0.0f,  1.0f};
    vivid::Param<float> chance    {"chance",     0.3f,  0.0f,  1.0f};
    vivid::Param<float> stop_time {"stop_time",  0.5f,  0.05f, 2.0f};
    vivid::Param<float> start_time{"start_time", 0.2f,  0.05f, 1.0f};
    vivid::Param<int>   mode      {"mode",       0, {"StopStart","Stop","Start"}};
    vivid::Param<float> mix       {"mix",        1.0f,  0.0f,  1.0f};

    glitch::CircularBuffer buf_;
    glitch::WhiteNoise     rng_;
    float prev_phase_ = 0.0f;

    enum State { Pass, Stopping, Stopped, Starting };
    State    state_         = Pass;
    uint32_t state_pos_     = 0;
    uint32_t state_len_     = 0;
    double   read_offset_   = 0.0; // fractional offset into buffer for varispeed
    uint32_t stopped_count_ = 0;
    uint32_t stopped_len_   = 0;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&phase);
        out.push_back(&chance);
        out.push_back(&stop_time);
        out.push_back(&start_time);
        out.push_back(&mode);
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
        uint32_t sr = audio->sample_rate;

        float cur_phase = phase.value;
        float wet = mix.value;
        float dry = 1.0f - wet;
        int   cur_mode = mode.int_value();

        // Trigger check
        if (glitch::detect_trigger(cur_phase, prev_phase_) && state_ == Pass) {
            if (rng_.next_unipolar() < chance.value) {
                if (cur_mode == 2) {
                    // Start mode: jump straight to Starting
                    state_     = Starting;
                    state_pos_ = 0;
                    state_len_ = static_cast<uint32_t>(start_time.value * sr);
                    if (state_len_ < 1) state_len_ = 1;
                    read_offset_ = 0.0;
                } else {
                    state_     = Stopping;
                    state_pos_ = 0;
                    state_len_ = static_cast<uint32_t>(stop_time.value * sr);
                    if (state_len_ < 1) state_len_ = 1;
                    read_offset_ = 0.0;
                }
            }
        }

        for (uint32_t i = 0; i < frames; i++) {
            buf_.write(in[i]);

            if (state_ == Stopping) {
                float progress = static_cast<float>(state_pos_) / static_cast<float>(state_len_);
                float rate = (1.0f - progress) * (1.0f - progress) * (1.0f - progress); // cubic

                read_offset_ += rate;
                uint32_t frames_back = static_cast<uint32_t>(state_len_ - read_offset_);
                if (frames_back >= buf_.size) frames_back = buf_.size - 1;
                float sample = buf_.read(static_cast<double>(buf_.get_read_pos(frames_back)));

                out[i] = in[i] * dry + sample * wet;

                state_pos_++;
                if (state_pos_ >= state_len_) {
                    if (cur_mode == 1) {
                        // Stop-only: return to passthrough
                        state_ = Pass;
                    } else {
                        state_       = Stopped;
                        stopped_count_ = 0;
                        stopped_len_   = sr / 10; // 100ms silence
                    }
                }
            } else if (state_ == Stopped) {
                out[i] = in[i] * dry; // silence for wet portion

                stopped_count_++;
                if (stopped_count_ >= stopped_len_) {
                    state_     = Starting;
                    state_pos_ = 0;
                    state_len_ = static_cast<uint32_t>(start_time.value * sr);
                    if (state_len_ < 1) state_len_ = 1;
                    read_offset_ = 0.0;
                }
            } else if (state_ == Starting) {
                float progress = static_cast<float>(state_pos_) / static_cast<float>(state_len_);
                float rate = progress * progress; // quadratic

                read_offset_ += rate;
                uint32_t frames_back = static_cast<uint32_t>(state_len_ - read_offset_);
                if (frames_back >= buf_.size) frames_back = buf_.size - 1;
                float sample = buf_.read(static_cast<double>(buf_.get_read_pos(frames_back)));

                out[i] = in[i] * dry + sample * wet;

                state_pos_++;
                if (state_pos_ >= state_len_) {
                    state_ = Pass;
                }
            } else {
                out[i] = in[i];
            }
        }

        prev_phase_ = cur_phase;
    }
};

VIVID_REGISTER(TapeStop)
