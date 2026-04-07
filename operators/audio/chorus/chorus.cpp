#include "operator_api/metronome_sync.h"
#include "operator_api/operator.h"
#include "operator_api/audio_dsp.h"

#include <cmath>
#include <vector>

// ---------------------------------------------------------------------------
// Chorus — multi-voice fractional delay lines with phase-offset LFOs (mono)
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

static constexpr int   kMaxVoices   = 6;
static constexpr float kCenterDelay = 0.007f;  // 7ms
static constexpr float kMaxDepth    = 0.005f;  // 5ms
static constexpr float kMaxDelay    = 0.015f;  // 15ms

/**
 * @brief Multi-voice chorus effect with modulated fractional delay.
 *
 * Mixes delayed copies of the input with phase-offset LFO modulation.
 * Each voice uses a slightly different delay time to create width and
 * movement. Center delay is 7ms with +/-5ms variation.
 *
 * @tip Increase voices for a thicker, more ensemble-like sound.
 * @see Flanger, Phaser, Delay
 */
struct Chorus : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "Chorus";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> rate  {"rate",   0.5f, 0.05f, 5.0f};
    vivid::Param<int>   rate_mode{"rate_mode", 0, vivid::rate_mode_labels()};
    vivid::Param<int>   sync_division{"sync_division", 2, vivid::metronome_division_labels()};
    vivid::Param<float> depth {"depth",  0.5f, 0.0f,  1.0f};
    vivid::Param<int>   voices{"voices", 3, 1, 6};
    vivid::Param<float> mix   {"mix",    0.5f, 0.0f,  1.0f};

    FractionalDelayLine delays_[kMaxVoices];
    double   phase_       = 0.0;
    float    prev_external_phase_ = 0.0f;
    int      external_beat_count_ = 0;
    bool     initialized_ = false;
    uint32_t init_rate_   = 0;

    Chorus() {
        vivid::semantic_tag(rate, "frequency_hz");
        vivid::semantic_shape(rate, "scalar");
        vivid::semantic_unit(rate, "Hz");
        vivid::display_hint(rate, VIVID_DISPLAY_KNOB);
        vivid::description(rate, "LFO modulation speed in Hz");
        vivid::description(rate_mode, "Free runs internally, follows an external beat_phase input, or locks to the graph metronome");
        vivid::description(sync_division, "Musical note length used when the chorus rate follows a clock");

        vivid::semantic_tag(depth, "probability_01");
        vivid::semantic_shape(depth, "scalar");
        vivid::display_hint(depth, VIVID_DISPLAY_KNOB);
        vivid::description(depth, "Amount of delay-time modulation (0 = none, 1 = full)");

        vivid::semantic_tag(voices, "count");
        vivid::semantic_shape(voices, "scalar");
        vivid::display_hint(voices, VIVID_DISPLAY_KNOB);
        vivid::description(voices, "Number of chorus voices (more = thicker)");

        vivid::semantic_tag(mix, "probability_01");
        vivid::semantic_shape(mix, "scalar");
        vivid::semantic_intent(mix, "wet_mix");
        vivid::display_hint(mix, VIVID_DISPLAY_KNOB);
        vivid::description(mix, "Dry/wet blend (0 = dry, 1 = fully chorused)");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&rate);
        out.push_back(&rate_mode);
        out.push_back(&sync_division);
        out.push_back(&depth);
        out.push_back(&voices);
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
        for (int v = 0; v < kMaxVoices; v++)
            delays_[v].init(max_samples);
        phase_ = 0.0;
        prev_external_phase_ = 0.0f;
        external_beat_count_ = 0;
        initialized_ = true;
        init_rate_   = sr;
    }

    void process_audio(const VividAudioContext* ctx) override {
        lazy_init(ctx->sample_rate);

        float* in  = ctx->input_buffers[0];
        float* out = ctx->output_buffers[0];
        uint32_t frames = ctx->buffer_size;

        float rate_cv_val = ctx->input_buffers[1] ? ctx->input_buffers[1][0] : 0.0f;
        const int mode = rate_mode.int_value();
        float mod_rate = rate.value + rate_cv_val;
        if (mod_rate < 0.05f) mod_rate = 0.05f;
        if (mod_rate > 5.0f)  mod_rate = 5.0f;

        int   voice_count = voices.int_value();
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
            // Write input to all delay lines (for seamless voice-count changes)
            for (int v = 0; v < kMaxVoices; v++)
                delays_[v].push(in[i]);

            double base_phase = phase_;
            if (mode == vivid::kRateModeMetronome) {
                base_phase = vivid::cycle_phase_from_total_beats(
                    metronome.beats_elapsed + static_cast<double>(i) * metronome_beats_per_sample,
                    sync_division.int_value());
            } else if (mode == vivid::kRateModeExternal) {
                float external_phase = ctx->input_buffers[2] ? ctx->input_buffers[2][i] : 0.0f;
                const double total_beats = vivid::advance_external_total_beats(
                    external_phase, prev_external_phase_, external_beat_count_);
                base_phase = vivid::cycle_phase_from_total_beats(total_beats, sync_division.int_value());
            }

            float sum = 0.0f;
            for (int v = 0; v < voice_count; v++) {
                double voice_phase = base_phase + static_cast<double>(v) / static_cast<double>(voice_count);
                if (voice_phase >= 1.0) voice_phase -= 1.0;

                float lfo = static_cast<float>(audio_dsp::waveform(voice_phase, 0)) * 0.5f + 0.5f;
                float delay_samples = center_samples + depth_samples * lfo;
                if (delay_samples < 1.0f) delay_samples = 1.0f;
                if (delay_samples >= static_cast<float>(delays_[v].size - 1))
                    delay_samples = static_cast<float>(delays_[v].size - 2);

                sum += delays_[v].read(delay_samples);
            }

            float wet_signal = sum / static_cast<float>(voice_count);
            out[i] = in[i] * dry + wet_signal * wet;

            if (mode == vivid::kRateModeFree) {
                phase_ += mod_rate * inv_sr;
                if (phase_ >= 1.0) phase_ -= 1.0;
            }
        }
    }
};

VIVID_REGISTER(Chorus)
