// Glitch audio effects ported from ../vivid-glitch into vivid-4 core built-ins (AO-3).
// The operator bodies are the classic ones; only the metadata (kName -> kDisplayName/
// kSummary/kKeywords) and registration (register_op instead of VIVID_REGISTER) change.
#include "gpu/op_runtime.h"          // OpRegistry / register_op (includes operator_api)
#include "audio/glitch/glitch_dsp.h"
#include "operator_api/metronome_sync.h"

#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace vivid {

namespace {
inline VividPortDescriptor aud_in()  { return { "input",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT  }; }
inline VividPortDescriptor aud_out() { return { "output", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT }; }
}

// --- Stutter: beat-synced slice repeater with envelope shapes -------------------------
struct StutterOp : OperatorBase, AudioProcessable {
    VividOperatorRole declared_operator_role() const override { return VIVID_OP_ROLE_TRANSFORM; }   // ADR-0046
    static constexpr const char* kDisplayName = "Stutter";
    static constexpr const char* kSummary = "Beat-synced slice repeater with envelope shapes (glitch stutter).";
    static constexpr std::array<const char*, 4> kKeywords = { "audio", "effect", "glitch", "stutter" };
    static constexpr uint32_t kMaxChannels = 2;

    Param<float> phase     { "phase",      0.0f, 0.0f,  1.0f };
    Param<int>   clock      { "clock",      0, { "Free", "External", "Metronome" } };
    Param<float> chance     { "chance",     0.5f, 0.0f,  1.0f };
    Param<float> size       { "size",       0.1f, 0.02f, 1.0f };
    Param<int>   division   { "division",   3, { "1/1","1/2","1/4","1/8","1/16","1/32","1/4T","1/8T","1/4D","1/8D" } };
    Param<int>   count      { "count",      8,    1,     32 };
    Param<int>   envelope   { "envelope",   0, { "Decay","Build","Flat","Triangle" } };
    Param<float> env_amount { "env_amount", 0.5f, 0.0f,  1.0f };
    Param<float> mix        { "mix",        1.0f, 0.0f,  1.0f };

    glitch::CircularBuffer buf_[kMaxChannels];
    glitch::WhiteNoise     rng_;
    glitch::TempoTracker   tempo_;
    float prev_phase_ = 0.0f;

    enum State { Passthrough, Stuttering };
    State    state_       = Passthrough;
    uint32_t slice_start_ = 0;
    uint32_t slice_len_   = 0;
    uint32_t slice_pos_   = 0;
    int      current_rep_ = 0;
    int      total_reps_  = 0;

    void collect_params(std::vector<ParamBase*>& o) override {
        o.push_back(&phase); o.push_back(&clock); o.push_back(&chance); o.push_back(&size);
        o.push_back(&division); o.push_back(&count); o.push_back(&envelope);
        o.push_back(&env_amount); o.push_back(&mix);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(aud_in()); o.push_back(aud_out()); }

    float envelope_gain(int rep, int total, int shape, float amount) const {
        if (total <= 1) return 1.0f;
        const float progress = static_cast<float>(rep) / static_cast<float>(total - 1);
        switch (shape) {
            case 0: return 1.0f - progress * amount;                                     // Decay
            case 1: return (1.0f - amount) + progress * amount;                          // Build
            case 2: return 1.0f;                                                         // Flat
            case 3: { const float tri = (progress < 0.5f) ? (progress * 2.0f) : (2.0f - progress * 2.0f);
                      return (1.0f - amount) + tri * amount; }                           // Triangle
            default: return 1.0f;
        }
    }

    void process_audio(const VividAudioContext* ctx) override {
        uint32_t nch = ctx->input_channel_counts ? ctx->input_channel_counts[0] : 1u;
        if (nch > kMaxChannels) nch = kMaxChannels;
        for (uint32_t c = 0; c < nch; c++) buf_[c].init(ctx->sample_rate);

        const uint32_t frames = ctx->buffer_size;
        const int clk = clock.int_value();
        const auto metro = vivid::metronome_transport(ctx);
        const float cur_phase = (clk == 2) ? metro.beat_phase : phase.value;

        const float wet = mix.value, dry = 1.0f - wet;
        const int   cnt = count.int_value();
        const int   env_shape = envelope.int_value();
        const float env_amt = env_amount.value;
        const bool  trigger_now = glitch::detect_trigger(cur_phase, prev_phase_);
        tempo_.update_block(frames, trigger_now);

        uint32_t slice_samples;
        if (clk == 2)
            slice_samples = glitch::samples_from_bpm(metro.bpm, division.int_value(), ctx->sample_rate);
        else
            slice_samples = glitch::resolve_tempo_locked_samples(clk == 1, size.value, division.int_value(),
                                tempo_, ctx->sample_rate, 1, buf_[0].size > 0 ? buf_[0].size - 1 : 0);

        if (trigger_now && state_ == Passthrough && rng_.next_unipolar() < chance.value) {
            state_ = Stuttering; slice_len_ = slice_samples;
            slice_start_ = buf_[0].get_read_pos(slice_len_);
            slice_pos_ = 0; current_rep_ = 0; total_reps_ = cnt;
        }

        for (uint32_t i = 0; i < frames; i++) {
            for (uint32_t c = 0; c < nch; c++) {
                const float* in_c  = ctx->input_buffers[0]  + c * frames;
                float*       out_c = ctx->output_buffers[0] + c * frames;
                buf_[c].write(in_c[i]);
                if (state_ == Stuttering) {
                    const double read_pos = static_cast<double>(slice_start_) + slice_pos_;
                    float sample = buf_[c].read(read_pos);
                    sample *= glitch::crossfade_coeff(slice_pos_, slice_len_, 64) * envelope_gain(current_rep_, total_reps_, env_shape, env_amt);
                    out_c[i] = in_c[i] * dry + sample * wet;
                } else {
                    out_c[i] = in_c[i];
                }
            }
            if (state_ == Stuttering && ++slice_pos_ >= slice_len_) {
                slice_pos_ = 0;
                if (++current_rep_ >= total_reps_) state_ = Passthrough;
            }
        }
        prev_phase_ = cur_phase;
    }
};

// --- TapeStop: tape-deck slowdown/restart (varispeed) --------------------------------
struct TapeStopOp : vivid::OperatorBase, vivid::AudioProcessable {
    VividOperatorRole declared_operator_role() const override { return VIVID_OP_ROLE_TRANSFORM; }   // ADR-0046
    static constexpr const char* kDisplayName = "Tape Stop";
    static constexpr const char* kSummary = "Tape-deck slowdown/restart (varispeed stop gesture).";
    static constexpr std::array<const char*, 4> kKeywords = { "audio", "effect", "glitch", "tape" };
    static constexpr uint32_t kMaxChannels = 2;

    vivid::Param<float> phase     {"phase",      0.0f,  0.0f,  1.0f};
    vivid::Param<int>   clock     {"clock",      0, {"External","Metronome"}};
    vivid::Param<float> chance    {"chance",     0.3f,  0.0f,  1.0f};
    vivid::Param<float> stop_time {"stop_time",  0.5f,  0.05f, 2.0f};
    vivid::Param<float> start_time{"start_time", 0.2f,  0.05f, 1.0f};
    vivid::Param<int>   mode      {"mode",       0, {"StopStart","Stop","Start"}};
    vivid::Param<float> mix       {"mix",        1.0f,  0.0f,  1.0f};

    glitch::CircularBuffer buf_[kMaxChannels];
    glitch::WhiteNoise     rng_;
    float prev_phase_ = 0.0f;

    enum State { Pass, Stopping, Stopped, Starting };
    State    state_         = Pass;
    uint32_t state_pos_     = 0;
    uint32_t state_len_     = 0;
    double   read_offset_   = 0.0;
    uint32_t stopped_count_ = 0;
    uint32_t stopped_len_   = 0;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&phase); out.push_back(&clock); out.push_back(&chance);
        out.push_back(&stop_time); out.push_back(&start_time); out.push_back(&mode); out.push_back(&mix);
    }
    void collect_ports(std::vector<VividPortDescriptor>& out) override { out.push_back(aud_in()); out.push_back(aud_out()); }

    void process_audio(const VividAudioContext* ctx) override {
        uint32_t nch = ctx->input_channel_counts ? ctx->input_channel_counts[0] : 1u;
        if (nch > kMaxChannels) nch = kMaxChannels;
        for (uint32_t c = 0; c < nch; c++) buf_[c].init(ctx->sample_rate);

        uint32_t frames = ctx->buffer_size;
        uint32_t sr = ctx->sample_rate;
        auto  metro = vivid::metronome_transport(ctx);
        float cur_phase = (clock.int_value() == 1) ? metro.beat_phase : phase.value;
        float wet = mix.value;
        float dry = 1.0f - wet;
        int   cur_mode = mode.int_value();

        if (glitch::detect_trigger(cur_phase, prev_phase_) && state_ == Pass) {
            if (rng_.next_unipolar() < chance.value) {
                if (cur_mode == 2) {
                    state_ = Starting; state_pos_ = 0;
                    state_len_ = static_cast<uint32_t>(start_time.value * sr);
                    if (state_len_ < 1) state_len_ = 1; read_offset_ = 0.0;
                } else {
                    state_ = Stopping; state_pos_ = 0;
                    state_len_ = static_cast<uint32_t>(stop_time.value * sr);
                    if (state_len_ < 1) state_len_ = 1; read_offset_ = 0.0;
                }
            }
        }

        for (uint32_t i = 0; i < frames; i++) {
            for (uint32_t c = 0; c < nch; c++) { const float* in_c = ctx->input_buffers[0] + c * frames; buf_[c].write(in_c[i]); }

            if (state_ == Stopping) {
                float progress = static_cast<float>(state_pos_) / static_cast<float>(state_len_);
                float rate = (1.0f - progress) * (1.0f - progress) * (1.0f - progress);
                read_offset_ += rate;
                uint32_t frames_back = static_cast<uint32_t>(state_len_ - read_offset_);
                if (frames_back >= buf_[0].size) frames_back = buf_[0].size - 1;
                uint32_t read_abs = buf_[0].get_read_pos(frames_back);
                for (uint32_t c = 0; c < nch; c++) {
                    const float* in_c = ctx->input_buffers[0] + c * frames; float* out_c = ctx->output_buffers[0] + c * frames;
                    float sample = buf_[c].read(static_cast<double>(read_abs));
                    out_c[i] = in_c[i] * dry + sample * wet;
                }
                state_pos_++;
                if (state_pos_ >= state_len_) {
                    if (cur_mode == 1) state_ = Pass;
                    else { state_ = Stopped; stopped_count_ = 0; stopped_len_ = sr / 10; }
                }
            } else if (state_ == Stopped) {
                for (uint32_t c = 0; c < nch; c++) {
                    const float* in_c = ctx->input_buffers[0] + c * frames; float* out_c = ctx->output_buffers[0] + c * frames;
                    out_c[i] = in_c[i] * dry;
                }
                if (++stopped_count_ >= stopped_len_) {
                    state_ = Starting; state_pos_ = 0;
                    state_len_ = static_cast<uint32_t>(start_time.value * sr);
                    if (state_len_ < 1) state_len_ = 1; read_offset_ = 0.0;
                }
            } else if (state_ == Starting) {
                float progress = static_cast<float>(state_pos_) / static_cast<float>(state_len_);
                float rate = progress * progress;
                read_offset_ += rate;
                uint32_t frames_back = static_cast<uint32_t>(state_len_ - read_offset_);
                if (frames_back >= buf_[0].size) frames_back = buf_[0].size - 1;
                uint32_t read_abs = buf_[0].get_read_pos(frames_back);
                for (uint32_t c = 0; c < nch; c++) {
                    const float* in_c = ctx->input_buffers[0] + c * frames; float* out_c = ctx->output_buffers[0] + c * frames;
                    float sample = buf_[c].read(static_cast<double>(read_abs));
                    out_c[i] = in_c[i] * dry + sample * wet;
                }
                state_pos_++;
                if (state_pos_ >= state_len_) state_ = Pass;
            } else {
                for (uint32_t c = 0; c < nch; c++) {
                    const float* in_c = ctx->input_buffers[0] + c * frames; float* out_c = ctx->output_buffers[0] + c * frames;
                    out_c[i] = in_c[i];
                }
            }
        }
        prev_phase_ = cur_phase;
    }
};

// --- BeatRepeat: beat-synced slice repeater with per-repeat decay ---------------------
struct BeatRepeatOp : vivid::OperatorBase, vivid::AudioProcessable {
    VividOperatorRole declared_operator_role() const override { return VIVID_OP_ROLE_TRANSFORM; }   // ADR-0046
    static constexpr const char* kDisplayName = "Beat Repeat";
    static constexpr const char* kSummary = "Beat-synced slice repeater with per-repeat decay.";
    static constexpr std::array<const char*, 4> kKeywords = { "audio", "effect", "glitch", "repeat" };
    static constexpr uint32_t kMaxChannels = 2;

    vivid::Param<float> phase   {"phase",    0.0f,  0.0f,  1.0f};
    vivid::Param<int>   clock   {"clock",    0, {"Free","External","Metronome"}};
    vivid::Param<float> chance  {"chance",   0.5f,  0.0f,  1.0f};
    vivid::Param<float> size    {"size",     0.15f, 0.02f, 1.0f};
    vivid::Param<int>   division{"division", 3, {"1/1","1/2","1/4","1/8","1/16","1/32","1/4T","1/8T","1/4D","1/8D"}};
    vivid::Param<int>   count   {"count",    4, 1, 16};
    vivid::Param<float> decay   {"decay",    0.1f,  0.0f,  1.0f};
    vivid::Param<float> mix     {"mix",      1.0f,  0.0f,  1.0f};

    glitch::CircularBuffer buf_[kMaxChannels];
    glitch::WhiteNoise     rng_;
    glitch::TempoTracker   tempo_;
    float prev_phase_ = 0.0f;

    enum State { Passthrough, Repeating };
    State    state_        = Passthrough;
    uint32_t slice_start_  = 0;
    uint32_t slice_len_    = 0;
    uint32_t slice_pos_    = 0;
    int      current_rep_  = 0;
    int      total_reps_   = 0;
    float    rep_gain_     = 1.0f;
    float    decay_factor_ = 1.0f;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&phase); out.push_back(&clock); out.push_back(&chance); out.push_back(&size);
        out.push_back(&division); out.push_back(&count); out.push_back(&decay); out.push_back(&mix);
    }
    void collect_ports(std::vector<VividPortDescriptor>& out) override { out.push_back(aud_in()); out.push_back(aud_out()); }

    void process_audio(const VividAudioContext* ctx) override {
        uint32_t nch = ctx->input_channel_counts ? ctx->input_channel_counts[0] : 1u;
        if (nch > kMaxChannels) nch = kMaxChannels;
        for (uint32_t c = 0; c < nch; c++) buf_[c].init(ctx->sample_rate);

        uint32_t frames = ctx->buffer_size;
        uint32_t sr     = ctx->sample_rate;
        int  clk = clock.int_value();
        auto metro = vivid::metronome_transport(ctx);
        float cur_phase = (clk == 2) ? metro.beat_phase : phase.value;
        float wet = mix.value;
        float dry = 1.0f - wet;
        bool trigger_now = glitch::detect_trigger(cur_phase, prev_phase_);
        tempo_.update_block(frames, trigger_now);

        uint32_t slice_samples;
        if (clk == 2) slice_samples = glitch::samples_from_bpm(metro.bpm, division.int_value(), sr);
        else slice_samples = glitch::resolve_tempo_locked_samples(clk == 1, size.value, division.int_value(),
                                 tempo_, sr, 1, buf_[0].size > 0 ? buf_[0].size - 1 : 0);

        if (trigger_now && state_ == Passthrough && rng_.next_unipolar() < chance.value) {
            state_ = Repeating; slice_len_ = slice_samples;
            slice_start_ = buf_[0].get_read_pos(slice_len_); slice_pos_ = 0;
            current_rep_ = 0; total_reps_ = count.int_value();
            rep_gain_ = 1.0f; decay_factor_ = 1.0f - decay.value;
        }

        for (uint32_t i = 0; i < frames; i++) {
            for (uint32_t c = 0; c < nch; c++) {
                const float* in_c = ctx->input_buffers[0] + c * frames; float* out_c = ctx->output_buffers[0] + c * frames;
                buf_[c].write(in_c[i]);
                if (state_ == Repeating) {
                    float sample = buf_[c].read(static_cast<double>(slice_start_) + slice_pos_);
                    sample *= glitch::crossfade_coeff(slice_pos_, slice_len_, 64) * rep_gain_;
                    out_c[i] = in_c[i] * dry + sample * wet;
                } else out_c[i] = in_c[i];
            }
            if (state_ == Repeating && ++slice_pos_ >= slice_len_) {
                slice_pos_ = 0; rep_gain_ *= decay_factor_;
                if (++current_rep_ >= total_reps_) state_ = Passthrough;
            }
        }
        prev_phase_ = cur_phase;
    }
};

// --- Reverse: beat-synced backward playback with crossfaded edges --------------------
struct ReverseOp : vivid::OperatorBase, vivid::AudioProcessable {
    VividOperatorRole declared_operator_role() const override { return VIVID_OP_ROLE_TRANSFORM; }   // ADR-0046
    static constexpr const char* kDisplayName = "Reverse";
    static constexpr const char* kSummary = "Beat-synced backward playback with crossfaded edges.";
    static constexpr std::array<const char*, 4> kKeywords = { "audio", "effect", "glitch", "reverse" };
    static constexpr uint32_t kMaxChannels = 2;

    vivid::Param<float> phase    {"phase",    0.0f,  0.0f, 1.0f};
    vivid::Param<int>   clock    {"clock",    0, {"Free","External","Metronome"}};
    vivid::Param<float> chance   {"chance",   0.5f,  0.0f, 1.0f};
    vivid::Param<float> size     {"size",     0.25f, 0.05f, 2.0f};
    vivid::Param<int>   division {"division", 2, {"1/1","1/2","1/4","1/8","1/16","1/32","1/4T","1/8T","1/4D","1/8D"}};
    vivid::Param<float> transition_ms{"transition_ms", 6.0f, 0.0f, 40.0f};
    vivid::Param<float> mix   {"mix",    1.0f,  0.0f, 1.0f};

    glitch::CircularBuffer buf_[kMaxChannels];
    glitch::WhiteNoise     rng_;
    glitch::TempoTracker   tempo_;
    float prev_phase_ = 0.0f;

    enum State { Passthrough, Reversing };
    State    state_       = Passthrough;
    uint32_t slice_end_   = 0;
    uint32_t slice_len_   = 0;
    uint32_t slice_pos_   = 0;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&phase); out.push_back(&clock); out.push_back(&chance); out.push_back(&size);
        out.push_back(&division); out.push_back(&transition_ms); out.push_back(&mix);
    }
    void collect_ports(std::vector<VividPortDescriptor>& out) override { out.push_back(aud_in()); out.push_back(aud_out()); }

    void process_audio(const VividAudioContext* ctx) override {
        uint32_t nch = ctx->input_channel_counts ? ctx->input_channel_counts[0] : 1u;
        if (nch > kMaxChannels) nch = kMaxChannels;
        for (uint32_t c = 0; c < nch; c++) buf_[c].init(ctx->sample_rate);

        uint32_t frames = ctx->buffer_size;
        int  clk = clock.int_value();
        auto metro = vivid::metronome_transport(ctx);
        float cur_phase = (clk == 2) ? metro.beat_phase : phase.value;
        float wet = mix.value;
        bool trigger_now = glitch::detect_trigger(cur_phase, prev_phase_);
        tempo_.update_block(frames, trigger_now);

        uint32_t slice_samples;
        if (clk == 2) slice_samples = glitch::samples_from_bpm(metro.bpm, division.int_value(), ctx->sample_rate);
        else slice_samples = glitch::resolve_tempo_locked_samples(clk == 1, size.value, division.int_value(),
                                 tempo_, ctx->sample_rate, 1, buf_[0].size > 0 ? buf_[0].size - 1 : 0);

        if (trigger_now && state_ == Passthrough && rng_.next_unipolar() < chance.value) {
            state_ = Reversing; slice_len_ = slice_samples;
            slice_end_ = buf_[0].get_read_pos(1); slice_pos_ = 0;
        }

        uint32_t transition_samples = static_cast<uint32_t>(transition_ms.value * 0.001f * static_cast<float>(ctx->sample_rate));

        for (uint32_t i = 0; i < frames; i++) {
            for (uint32_t c = 0; c < nch; c++) {
                const float* in_c = ctx->input_buffers[0] + c * frames; float* out_c = ctx->output_buffers[0] + c * frames;
                buf_[c].write(in_c[i]);
                if (state_ == Reversing) {
                    float sample = buf_[c].read_reverse(slice_end_, static_cast<double>(slice_pos_));
                    sample *= glitch::crossfade_coeff(slice_pos_, slice_len_, 128);
                    float wet_gain = wet * glitch::crossfade_coeff(slice_pos_, slice_len_, transition_samples);
                    out_c[i] = in_c[i] * (1.0f - wet_gain) + sample * wet_gain;
                } else out_c[i] = in_c[i];
            }
            if (state_ == Reversing && ++slice_pos_ >= slice_len_) state_ = Passthrough;
        }
        prev_phase_ = cur_phase;
    }
};

// --- Scratch: DJ-style varispeed scratch with motion patterns ------------------------
struct ScratchOp : vivid::OperatorBase, vivid::AudioProcessable {
    VividOperatorRole declared_operator_role() const override { return VIVID_OP_ROLE_TRANSFORM; }   // ADR-0046
    static constexpr const char* kDisplayName = "Scratch";
    static constexpr const char* kSummary = "DJ-style varispeed scratch with motion patterns.";
    static constexpr std::array<const char*, 4> kKeywords = { "audio", "effect", "glitch", "scratch" };
    static constexpr uint32_t kMaxChannels = 2;

    vivid::Param<float> phase     {"phase",      0.0f,   0.0f,   1.0f};
    vivid::Param<int>   clock     {"clock",      0, {"External","Metronome"}};
    vivid::Param<float> chance    {"chance",     0.4f,   0.0f,   1.0f};
    vivid::Param<float> size      {"size",       0.3f,   0.05f,  2.0f};
    vivid::Param<float> speed     {"speed",      1.0f,   0.125f, 4.0f};
    vivid::Param<float> speed_rand{"speed_rand", 0.3f,   0.0f,   1.0f};
    vivid::Param<int>   motion    {"motion",     0, {"BackForth","Forward","Backward","Random"}};
    vivid::Param<float> mix       {"mix",        1.0f,   0.0f,   1.0f};

    glitch::CircularBuffer buf_[kMaxChannels];
    glitch::WhiteNoise     rng_;
    float prev_phase_ = 0.0f;

    enum State { Passthrough, Scratching };
    State    state_        = Passthrough;
    uint32_t region_start_ = 0;
    uint32_t region_len_   = 0;
    uint32_t total_len_    = 0;
    uint32_t total_pos_    = 0;
    double   read_pos_     = 0.0;
    float    direction_    = 1.0f;
    float    cur_speed_    = 1.0f;

    static constexpr float kSpeedPool[6] = {0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f};
    static constexpr int   kSpeedPoolSize = 6;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&phase); out.push_back(&clock); out.push_back(&chance); out.push_back(&size);
        out.push_back(&speed); out.push_back(&speed_rand); out.push_back(&motion); out.push_back(&mix);
    }
    void collect_ports(std::vector<VividPortDescriptor>& out) override { out.push_back(aud_in()); out.push_back(aud_out()); }

    float pick_speed() {
        float base = speed.value, rand_amt = speed_rand.value;
        if (rand_amt > 0.0f) {
            int idx = static_cast<int>(rng_.next_unipolar() * kSpeedPoolSize);
            if (idx >= kSpeedPoolSize) idx = kSpeedPoolSize - 1;
            return base * (1.0f - rand_amt) + kSpeedPool[idx] * rand_amt;
        }
        return base;
    }

    void process_audio(const VividAudioContext* ctx) override {
        uint32_t nch = ctx->input_channel_counts ? ctx->input_channel_counts[0] : 1u;
        if (nch > kMaxChannels) nch = kMaxChannels;
        for (uint32_t c = 0; c < nch; c++) buf_[c].init(ctx->sample_rate);

        uint32_t frames = ctx->buffer_size;
        auto  metro = vivid::metronome_transport(ctx);
        float cur_phase = (clock.int_value() == 1) ? metro.beat_phase : phase.value;
        float wet = mix.value;
        float dry = 1.0f - wet;
        int   cur_motion = motion.int_value();
        uint32_t scratch_samples = static_cast<uint32_t>(size.value * ctx->sample_rate);
        if (scratch_samples < 1) scratch_samples = 1;

        if (glitch::detect_trigger(cur_phase, prev_phase_) && state_ == Passthrough && rng_.next_unipolar() < chance.value) {
            state_ = Scratching; region_len_ = scratch_samples * 2;
            if (region_len_ >= buf_[0].size) region_len_ = buf_[0].size - 1;
            region_start_ = buf_[0].get_read_pos(region_len_);
            total_len_ = scratch_samples; total_pos_ = 0; read_pos_ = 0.0;
            cur_speed_ = pick_speed(); direction_ = (cur_motion == 2) ? -1.0f : 1.0f;
        }

        for (uint32_t i = 0; i < frames; i++) {
            if (state_ == Scratching) {
                if (cur_motion == 0) {
                    float half = static_cast<float>(total_len_) * 0.5f;
                    direction_ = (total_pos_ < static_cast<uint32_t>(half)) ? 1.0f : -1.0f;
                } else if (cur_motion == 3 && rng_.next_unipolar() < 0.02f) {
                    direction_ = -direction_; cur_speed_ = pick_speed();
                }
            }
            for (uint32_t c = 0; c < nch; c++) {
                const float* in_c = ctx->input_buffers[0] + c * frames; float* out_c = ctx->output_buffers[0] + c * frames;
                buf_[c].write(in_c[i]);
                if (state_ == Scratching) {
                    float sample = buf_[c].read(static_cast<double>(region_start_) + read_pos_);
                    sample *= glitch::crossfade_coeff(total_pos_, total_len_, 128);
                    out_c[i] = in_c[i] * dry + sample * wet;
                } else out_c[i] = in_c[i];
            }
            if (state_ == Scratching) {
                read_pos_ += direction_ * cur_speed_;
                if (read_pos_ < 0.0) read_pos_ = 0.0;
                if (read_pos_ >= static_cast<double>(region_len_)) read_pos_ = static_cast<double>(region_len_) - 1.0;
                if (++total_pos_ >= total_len_) state_ = Passthrough;
            }
        }
        prev_phase_ = cur_phase;
    }
};

// --- FreqShift: Bode frequency shifter (Hilbert) with LFO ----------------------------
struct FreqShiftOp : vivid::OperatorBase, vivid::AudioProcessable {
    VividOperatorRole declared_operator_role() const override { return VIVID_OP_ROLE_TRANSFORM; }   // ADR-0046
    static constexpr const char* kDisplayName = "Freq Shift";
    static constexpr const char* kSummary = "Bode frequency shifter (Hilbert) with LFO modulation.";
    static constexpr std::array<const char*, 4> kKeywords = { "audio", "effect", "glitch", "frequency" };
    static constexpr uint32_t kMaxChannels = 2;
    static constexpr int kHilbertTaps = 31;
    static constexpr int kHilbertHalf = kHilbertTaps / 2;

    vivid::Param<float> phase    {"phase",     0.0f,    0.0f,    1.0f};
    vivid::Param<float> shift    {"shift",     20.0f, -500.0f, 500.0f};
    vivid::Param<float> mod_depth{"mod_depth",  0.0f,   0.0f, 200.0f};
    vivid::Param<float> mod_rate {"mod_rate",   2.0f,   0.1f,  20.0f};
    vivid::Param<float> mix      {"mix",        1.0f,   0.0f,   1.0f};

    float hilbert_coeff_[kHilbertTaps] = {};
    bool  coeffs_ready_ = false;
    float  delay_line_[kMaxChannels][kHilbertTaps] = {};
    int    delay_idx_[kMaxChannels]                 = {};
    double osc_phase_[kMaxChannels]                 = {};
    double lfo_phase_[kMaxChannels]                 = {};

    void compute_coefficients() {
        if (coeffs_ready_) return;
        coeffs_ready_ = true;
        for (int i = 0; i < kHilbertTaps; i++) {
            int n = i - kHilbertHalf;
            hilbert_coeff_[i] = (n == 0 || (n % 2) == 0) ? 0.0f : 2.0f / (static_cast<float>(M_PI) * n);
            float w = 0.42f - 0.5f * std::cos(2.0f * static_cast<float>(M_PI) * i / (kHilbertTaps - 1))
                            + 0.08f * std::cos(4.0f * static_cast<float>(M_PI) * i / (kHilbertTaps - 1));
            hilbert_coeff_[i] *= w;
        }
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&phase); out.push_back(&shift); out.push_back(&mod_depth); out.push_back(&mod_rate); out.push_back(&mix);
    }
    void collect_ports(std::vector<VividPortDescriptor>& out) override { out.push_back(aud_in()); out.push_back(aud_out()); }

    void process_audio(const VividAudioContext* ctx) override {
        compute_coefficients();
        uint32_t nch = ctx->input_channel_counts ? ctx->input_channel_counts[0] : 1u;
        if (nch > kMaxChannels) nch = kMaxChannels;
        uint32_t frames = ctx->buffer_size;
        double inv_sr = 1.0 / static_cast<double>(ctx->sample_rate);
        float base_shift = shift.value, depth = mod_depth.value, rate = mod_rate.value;
        float wet = mix.value, dry = 1.0f - wet;

        for (uint32_t c = 0; c < nch; c++) {
            const float* in_c = ctx->input_buffers[0] + c * frames; float* out_c = ctx->output_buffers[0] + c * frames;
            int& didx = delay_idx_[c]; double& osc = osc_phase_[c]; double& lfo = lfo_phase_[c]; float* dline = delay_line_[c];
            for (uint32_t i = 0; i < frames; i++) {
                dline[didx] = in_c[i];
                float q_signal = 0.0f;
                for (int t = 0; t < kHilbertTaps; t++) { int idx = didx - t; if (idx < 0) idx += kHilbertTaps; q_signal += dline[idx] * hilbert_coeff_[t]; }
                int i_idx = didx - kHilbertHalf; if (i_idx < 0) i_idx += kHilbertTaps; float i_signal = dline[i_idx];
                float effective_shift = base_shift + static_cast<float>(std::sin(lfo * 2.0 * M_PI)) * depth;
                float cos_osc = static_cast<float>(std::cos(osc * 2.0 * M_PI));
                float sin_osc = static_cast<float>(std::sin(osc * 2.0 * M_PI));
                out_c[i] = in_c[i] * dry + (i_signal * cos_osc - q_signal * sin_osc) * wet;
                osc += effective_shift * inv_sr; osc -= std::floor(osc);
                lfo += rate * inv_sr; lfo -= std::floor(lfo);
                if (++didx >= kHilbertTaps) didx = 0;
            }
        }
    }
};

// --- Stretch: granular time-stretch (pitch preserved) --------------------------------
struct StretchOp : vivid::OperatorBase, vivid::AudioProcessable {
    VividOperatorRole declared_operator_role() const override { return VIVID_OP_ROLE_TRANSFORM; }   // ADR-0046
    static constexpr const char* kDisplayName = "Stretch";
    static constexpr const char* kSummary = "Granular time-stretch (pitch preserved).";
    static constexpr std::array<const char*, 4> kKeywords = { "audio", "effect", "glitch", "stretch" };
    static constexpr uint32_t kMaxChannels = 2;
    static constexpr int kMaxGrains = 8;

    vivid::Param<float> phase     {"phase",      0.0f, 0.0f,  1.0f};
    vivid::Param<float> chance    {"chance",     0.3f, 0.0f,  1.0f};
    vivid::Param<float> size      {"size",       0.3f, 0.05f, 2.0f};
    vivid::Param<float> factor    {"factor",     2.0f, 0.25f, 4.0f};
    vivid::Param<float> grain_size{"grain_size", 0.05f, 0.01f, 0.2f};
    vivid::Param<float> grain_rand{"grain_rand", 0.1f,  0.0f,  0.5f};
    vivid::Param<float> overlap   {"overlap",    0.5f,  0.25f, 0.75f};
    vivid::Param<float> mix       {"mix",        1.0f,  0.0f,  1.0f};

    glitch::CircularBuffer buf_[kMaxChannels];
    glitch::WhiteNoise     rng_;
    float prev_phase_ = 0.0f;

    struct Grain { bool active = false; uint32_t pos = 0; uint32_t length = 0; double buf_start = 0.0; };
    enum State { Passthrough, Stretching };
    State    state_            = Passthrough;
    uint32_t source_start_     = 0;
    uint32_t source_len_       = 0;
    uint32_t total_len_        = 0;
    uint32_t total_pos_        = 0;
    double   source_phase_     = 0.0;
    uint32_t next_grain_timer_ = 0;
    Grain    grains_[kMaxGrains];

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&phase); out.push_back(&chance); out.push_back(&size); out.push_back(&factor);
        out.push_back(&grain_size); out.push_back(&grain_rand); out.push_back(&overlap); out.push_back(&mix);
    }
    void collect_ports(std::vector<VividPortDescriptor>& out) override { out.push_back(aud_in()); out.push_back(aud_out()); }

    float hann_window(uint32_t pos, uint32_t length) const {
        if (length <= 1) return 1.0f;
        float t = static_cast<float>(pos) / static_cast<float>(length - 1);
        return 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * t));
    }
    void spawn_grain(uint32_t sr) {
        uint32_t g_len = static_cast<uint32_t>(grain_size.value * sr);
        if (g_len < 1) g_len = 1;
        double src_pos = source_phase_ * source_len_;
        src_pos += rng_.next() * grain_rand.value * static_cast<float>(g_len);
        if (src_pos < 0.0) src_pos = 0.0;
        if (src_pos >= source_len_) src_pos = static_cast<double>(source_len_) - 1.0;
        double buf_pos = static_cast<double>(source_start_) + src_pos;
        for (int g = 0; g < kMaxGrains; g++)
            if (!grains_[g].active) { grains_[g] = Grain{ true, 0, g_len, buf_pos }; break; }
    }

    void process_audio(const VividAudioContext* ctx) override {
        uint32_t nch = ctx->input_channel_counts ? ctx->input_channel_counts[0] : 1u;
        if (nch > kMaxChannels) nch = kMaxChannels;
        for (uint32_t c = 0; c < nch; c++) buf_[c].init(ctx->sample_rate);

        uint32_t frames = ctx->buffer_size;
        uint32_t sr = ctx->sample_rate;
        float cur_phase = phase.value;
        float wet = mix.value, dry = 1.0f - wet, fct = factor.value;
        uint32_t src_samples = static_cast<uint32_t>(size.value * sr);
        if (src_samples < 1) src_samples = 1;

        if (glitch::detect_trigger(cur_phase, prev_phase_) && state_ == Passthrough && rng_.next_unipolar() < chance.value) {
            state_ = Stretching; source_len_ = src_samples;
            source_start_ = buf_[0].get_read_pos(source_len_);
            total_len_ = static_cast<uint32_t>(source_len_ * fct);
            if (total_len_ < 1) total_len_ = 1;
            total_pos_ = 0; source_phase_ = 0.0; next_grain_timer_ = 0;
            for (int g = 0; g < kMaxGrains; g++) grains_[g].active = false;
        }

        uint32_t g_len = static_cast<uint32_t>(grain_size.value * sr); if (g_len < 1) g_len = 1;
        uint32_t spawn_interval = static_cast<uint32_t>(g_len * (1.0f - overlap.value)); if (spawn_interval < 1) spawn_interval = 1;
        float norm = 1.0f / ((static_cast<float>(g_len) / static_cast<float>(spawn_interval)) * 0.5f);

        for (uint32_t i = 0; i < frames; i++) {
            for (uint32_t c = 0; c < nch; c++) { const float* in_c = ctx->input_buffers[0] + c * frames; buf_[c].write(in_c[i]); }
            if (state_ == Stretching) {
                if (next_grain_timer_ == 0) { spawn_grain(sr); next_grain_timer_ = spawn_interval; }
                next_grain_timer_--;
                for (uint32_t c = 0; c < nch; c++) {
                    const float* in_c = ctx->input_buffers[0] + c * frames; float* out_c = ctx->output_buffers[0] + c * frames;
                    float sample = 0.0f;
                    for (int g = 0; g < kMaxGrains; g++) {
                        if (!grains_[g].active) continue;
                        sample += buf_[c].read(grains_[g].buf_start + grains_[g].pos) * hann_window(grains_[g].pos, grains_[g].length);
                    }
                    sample *= norm * glitch::crossfade_coeff(total_pos_, total_len_, 128);
                    out_c[i] = in_c[i] * dry + sample * wet;
                }
                for (int g = 0; g < kMaxGrains; g++) { if (!grains_[g].active) continue; if (++grains_[g].pos >= grains_[g].length) grains_[g].active = false; }
                source_phase_ += 1.0 / static_cast<double>(total_len_);
                if (source_phase_ > 1.0) source_phase_ = 1.0;
                if (++total_pos_ >= total_len_) state_ = Passthrough;
            } else {
                for (uint32_t c = 0; c < nch; c++) { const float* in_c = ctx->input_buffers[0] + c * frames; float* out_c = ctx->output_buffers[0] + c * frames; out_c[i] = in_c[i]; }
            }
        }
        prev_phase_ = cur_phase;
    }
};

void register_glitch_ops(OpRegistry& reg) {
    register_op<StutterOp>(reg, "Stutter");
    register_op<TapeStopOp>(reg, "TapeStop");
    register_op<BeatRepeatOp>(reg, "BeatRepeat");
    register_op<ReverseOp>(reg, "Reverse");
    register_op<ScratchOp>(reg, "Scratch");
    register_op<FreqShiftOp>(reg, "FreqShift");
    register_op<StretchOp>(reg, "Stretch");
}

}  // namespace vivid
