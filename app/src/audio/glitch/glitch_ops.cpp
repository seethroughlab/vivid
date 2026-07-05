// Glitch audio effects ported from ../vivid-glitch into vivid-4 core built-ins (AO-3).
// The operator bodies are the classic ones; only the metadata (kName -> kDisplayName/
// kSummary/kKeywords) and registration (register_op instead of VIVID_REGISTER) change.
#include "gpu/op_runtime.h"          // OpRegistry / register_op (includes operator_api)
#include "audio/glitch/glitch_dsp.h"
#include "operator_api/metronome_sync.h"

#include <array>
#include <vector>

namespace vivid {

namespace {
inline VividPortDescriptor aud_in()  { return { "input",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT  }; }
inline VividPortDescriptor aud_out() { return { "output", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT }; }
}

// --- Stutter: beat-synced slice repeater with envelope shapes -------------------------
struct StutterOp : OperatorBase, AudioProcessable {
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

void register_glitch_ops(OpRegistry& reg) {
    register_op<StutterOp>(reg, "Stutter");
}

}  // namespace vivid
