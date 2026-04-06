#include "operator_api/metronome_sync.h"
#include "operator_api/operator.h"
#include "operator_api/audio_dsp.h"

#include <cmath>
#include <vector>

// ---------------------------------------------------------------------------
// Flanger — fractional delay line modulated by sine LFO with feedback (mono)
// ---------------------------------------------------------------------------

struct FractionalDelayLine {
    std::vector<float> buffer;
    int size  = 0;
    int write = 0;

    void init(int max_samples) {
        size  = max_samples;
        write = 0;
        if (static_cast<int>(buffer.size()) < size) {
            buffer.assign(size, 0.0f);
        } else {
            std::fill_n(buffer.data(), size, 0.0f);
        }
    }

    void push(float v) {
        buffer[write] = v;
        if (++write >= size) write = 0;
    }

    float read(float delay_samples) const {
        float idx_f = static_cast<float>(write) - delay_samples;
        if (idx_f < 0.0f) idx_f += static_cast<float>(size);
        int idx0 = static_cast<int>(idx_f);
        int idx1 = idx0 + 1;
        if (idx0 >= size) idx0 -= size;
        if (idx1 >= size) idx1 -= size;
        if (idx0 < 0) idx0 += size;
        if (idx1 < 0) idx1 += size;
        float frac = idx_f - std::floor(idx_f);
        return buffer[idx0] * (1.0f - frac) + buffer[idx1] * frac;
    }
};

static constexpr float kCenterDelay = 0.002f;  // 2ms
static constexpr float kMaxDepth    = 0.003f;  // 3ms
static constexpr float kMaxDelay    = 0.006f;  // 6ms

/**
 * @brief Sine-modulated fractional delay with feedback for jet-like sweeps.
 *
 * Short delay line modulated by a sine LFO, creating comb-filtering that
 * sweeps through the spectrum. Negative feedback inverts the comb pattern.
 *
 * @see Chorus, Phaser, Delay
 */
struct Flanger : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "Flanger";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> rate    {"rate",      0.25f, 0.01f, 10.0f};
    vivid::Param<int>   rate_mode{"rate_mode", 0, vivid::rate_mode_labels()};
    vivid::Param<int>   sync_division{"sync_division", 2, vivid::metronome_division_labels()};
    vivid::Param<float> depth   {"depth",     0.7f,  0.0f,   1.0f};
    vivid::Param<float> feedback{"feedback",  0.5f, -0.95f,  0.95f};
    vivid::Param<float> mix     {"mix",       0.5f,  0.0f,   1.0f};

    FractionalDelayLine delay_;
    double   phase_       = 0.0;
    float    prev_external_phase_ = 0.0f;
    int      external_beat_count_ = 0;
    bool     initialized_ = false;
    uint32_t init_rate_   = 0;

    // DC blocker state
    float dc_x1_ = 0.0f;
    float dc_y1_ = 0.0f;

    Flanger() {
        vivid::semantic_tag(rate, "frequency_hz");
        vivid::semantic_shape(rate, "scalar");
        vivid::semantic_unit(rate, "Hz");
        vivid::display_hint(rate, VIVID_DISPLAY_KNOB);
        vivid::description(rate, "Speed of the LFO sweep in Hz");
        vivid::description(rate_mode, "Free runs internally, follows an external beat_phase input, or locks to the graph metronome");
        vivid::description(sync_division, "Musical note length used when the flanger rate follows a clock");

        vivid::semantic_tag(depth, "probability_01");
        vivid::semantic_shape(depth, "scalar");
        vivid::display_hint(depth, VIVID_DISPLAY_KNOB);
        vivid::description(depth, "How far the delay time sweeps from center (0 = none, 1 = full)");

        vivid::semantic_tag(feedback, "probability_01");
        vivid::semantic_shape(feedback, "scalar");
        vivid::display_hint(feedback, VIVID_DISPLAY_KNOB);
        vivid::description(feedback, "Amount of output fed back into the delay; negative values invert the comb pattern");

        vivid::semantic_tag(mix, "probability_01");
        vivid::semantic_shape(mix, "scalar");
        vivid::semantic_intent(mix, "wet_mix");
        vivid::display_hint(mix, VIVID_DISPLAY_KNOB);
        vivid::description(mix, "Blend between dry input and flanged signal");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&rate);
        out.push_back(&rate_mode);
        out.push_back(&sync_division);
        out.push_back(&depth);
        out.push_back(&feedback);
        out.push_back(&mix);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",   VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"output",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"rate_cv", VIVID_PORT_SCALAR, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        out.push_back({"beat_phase", VIVID_PORT_SCALAR, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        vivid::append_analysis_ports(out);
    }

    void lazy_init(uint32_t sr) {
        if (initialized_ && init_rate_ == sr) return;
        int max_samples = static_cast<int>(kMaxDelay * sr) + 2;
        delay_.init(max_samples);
        phase_  = 0.0;
        prev_external_phase_ = 0.0f;
        external_beat_count_ = 0;
        dc_x1_  = 0.0f;
        dc_y1_  = 0.0f;
        initialized_ = true;
        init_rate_   = sr;
    }

    float dc_block(float x) {
        float y = x - dc_x1_ + 0.995f * dc_y1_;
        dc_x1_ = x;
        dc_y1_ = y;
        return y;
    }

    void process_audio(const VividAudioContext* ctx) override {
        lazy_init(ctx->sample_rate);

        float* in  = ctx->input_buffers[0];
        float* out = ctx->output_buffers[0];
        uint32_t frames = ctx->buffer_size;

        float rate_cv_val = ctx->input_buffers[1] ? ctx->input_buffers[1][0] : 0.0f;
        const int mode = rate_mode.int_value();
        float mod_rate = rate.value + rate_cv_val;
        if (mod_rate < 0.01f) mod_rate = 0.01f;
        if (mod_rate > 10.0f) mod_rate = 10.0f;

        float fb  = feedback.value;
        float wet = mix.value;
        float dry = 1.0f - wet;
        float d   = depth.value;
        float sr  = static_cast<float>(ctx->sample_rate);
        double inv_sr = 1.0 / static_cast<double>(ctx->sample_rate);
        const vivid::MetronomeTransport metronome = vivid::metronome_transport(ctx);
        const double metronome_beats_per_sample = (metronome.bpm > 0.0f)
            ? (static_cast<double>(metronome.bpm) / 60.0) / static_cast<double>(ctx->sample_rate)
            : 0.0;

        float center_samples = kCenterDelay * sr;
        float depth_samples  = kMaxDepth * d * sr;

        for (uint32_t i = 0; i < frames; i++) {
            double phase = phase_;
            if (mode == vivid::kRateModeMetronome) {
                phase = metronome.enabled
                    ? vivid::cycle_phase_from_total_beats(
                          metronome.beats_elapsed + static_cast<double>(i) * metronome_beats_per_sample,
                          sync_division.int_value())
                    : 0.0;
            } else if (mode == vivid::kRateModeExternal) {
                float external_phase = ctx->input_buffers[2] ? ctx->input_buffers[2][i] : 0.0f;
                const double total_beats = vivid::advance_external_total_beats(
                    external_phase, prev_external_phase_, external_beat_count_);
                phase = vivid::cycle_phase_from_total_beats(total_beats, sync_division.int_value());
            }

            // Sine LFO [0,1] range
            float lfo = static_cast<float>(audio_dsp::waveform(phase, 0)) * 0.5f + 0.5f;
            if (mode == vivid::kRateModeFree) {
                phase_ += mod_rate * inv_sr;
                if (phase_ >= 1.0) phase_ -= 1.0;
            }

            float delay_samples = center_samples + depth_samples * lfo;
            if (delay_samples < 1.0f) delay_samples = 1.0f;
            if (delay_samples >= static_cast<float>(delay_.size - 1))
                delay_samples = static_cast<float>(delay_.size - 2);

            float delayed = delay_.read(delay_samples);
            float fb_signal = dc_block(delayed);
            delay_.push(in[i] + fb_signal * fb);
            out[i] = in[i] * dry + delayed * wet;
        }
    }
};

VIVID_REGISTER(Flanger)
