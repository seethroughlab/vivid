#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "operator_api/audio_dsp.h"

#include <cmath>
#include <vector>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------------------
// Granular Synth — grain cloud engine with capture buffer (mono)
// ---------------------------------------------------------------------------

static constexpr int   kMaxGrains      = 32;
static constexpr float kMaxCaptureSec  = 4.0f;

struct CaptureBuffer {
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

    // Read with linear interpolation at fractional sample index (absolute)
    float read_linear(float abs_pos) const {
        // Wrap into valid range
        float idx_f = std::fmod(abs_pos, static_cast<float>(size));
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

struct Grain {
    bool  active        = false;
    float start_pos     = 0.0f; // absolute sample position in capture buffer
    int   length        = 0;    // grain length in samples
    float cursor        = 0.0f; // playback position within grain (samples)
    float playback_rate = 1.0f;
    int   window_type   = 0;    // 0=Hann, 1=Hamming, 2=Blackman, 3=Triangle
};

static float grain_window(float phase, int type) {
    // phase in [0, 1]
    switch (type) {
        default:
        case 0: // Hann
            return 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * phase));
        case 1: // Hamming
            return 0.54f - 0.46f * std::cos(2.0f * static_cast<float>(M_PI) * phase);
        case 2: // Blackman
            return 0.42f - 0.5f * std::cos(2.0f * static_cast<float>(M_PI) * phase)
                         + 0.08f * std::cos(4.0f * static_cast<float>(M_PI) * phase);
        case 3: // Triangle
            return 1.0f - std::fabs(2.0f * phase - 1.0f);
    }
}

struct GranularSynth : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "GranularSynth";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> grain_size{"grain_size", 80.0f,  5.0f,  500.0f};
    vivid::Param<float> density   {"density",    10.0f,  0.5f,  60.0f};
    vivid::Param<float> position  {"position",   0.8f,   0.0f,  1.0f};
    vivid::Param<float> pitch     {"pitch",      0.0f,  -24.0f, 24.0f};
    vivid::Param<float> randomize {"randomize",  0.1f,   0.0f,  1.0f};
    vivid::Param<int>   window    {"window",     0, {"Hann", "Hamming", "Blackman", "Triangle"}};
    vivid::Param<float> mix       {"mix",        1.0f,   0.0f,  1.0f};

    CaptureBuffer  capture_;
    Grain          grains_[kMaxGrains];
    double         sched_phase_ = 0.0;
    audio_dsp::WhiteNoise rng_;
    bool           initialized_ = false;
    uint32_t       init_rate_   = 0;

    GranularSynth() {
        vivid::semantic_tag(grain_size, "time_milliseconds");
        vivid::semantic_shape(grain_size, "scalar");
        vivid::semantic_unit(grain_size, "ms");
        vivid::display_hint(grain_size, VIVID_DISPLAY_KNOB);

        vivid::semantic_tag(density, "frequency_hz");
        vivid::semantic_shape(density, "scalar");
        vivid::semantic_unit(density, "Hz");
        vivid::display_hint(density, VIVID_DISPLAY_KNOB);

        vivid::semantic_tag(position, "probability_01");
        vivid::semantic_shape(position, "scalar");
        vivid::display_hint(position, VIVID_DISPLAY_KNOB);

        vivid::semantic_tag(pitch, "semitones");
        vivid::semantic_shape(pitch, "scalar");
        vivid::semantic_unit(pitch, "st");
        vivid::display_hint(pitch, VIVID_DISPLAY_KNOB);

        vivid::semantic_tag(randomize, "probability_01");
        vivid::semantic_shape(randomize, "scalar");
        vivid::display_hint(randomize, VIVID_DISPLAY_KNOB);

        vivid::display_hint(window, VIVID_DISPLAY_KNOB);

        vivid::semantic_tag(mix, "probability_01");
        vivid::semantic_shape(mix, "scalar");
        vivid::semantic_intent(mix, "wet_mix");
        vivid::display_hint(mix, VIVID_DISPLAY_KNOB);
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&grain_size);
        out.push_back(&density);
        out.push_back(&position);
        out.push_back(&pitch);
        out.push_back(&randomize);
        out.push_back(&window);
        out.push_back(&mix);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",      VIVID_PORT_AUDIO, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"output",     VIVID_PORT_AUDIO, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"position_cv", VIVID_PORT_SIGNAL, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        out.push_back({"pitch_cv",    VIVID_PORT_SIGNAL, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        out.push_back({"density_cv",  VIVID_PORT_SIGNAL, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
    }

    void lazy_init(uint32_t sr) {
        if (initialized_ && init_rate_ == sr) return;
        int cap_samples = static_cast<int>(kMaxCaptureSec * sr) + 2;
        capture_.init(cap_samples);
        for (int g = 0; g < kMaxGrains; g++)
            grains_[g].active = false;
        sched_phase_ = 0.0;
        initialized_ = true;
        init_rate_   = sr;
    }

    void spawn_grain(float sr, float pos_param, float pitch_param, float size_ms,
                     float rand_amt, int win_type) {
        // Find inactive slot
        int slot = -1;
        for (int g = 0; g < kMaxGrains; g++) {
            if (!grains_[g].active) { slot = g; break; }
        }
        if (slot < 0) return; // all slots busy

        Grain& grain = grains_[slot];

        // Jitter position: up to 10% of buffer
        float pos_jitter = rand_amt * 0.1f * rng_.next();
        float pos = pos_param + pos_jitter;
        pos = std::fmax(0.0f, std::fmin(1.0f, pos));

        // Start position: how far back from write head
        float delay_samples = pos * static_cast<float>(capture_.size);
        grain.start_pos = std::fmod(
            static_cast<float>(capture_.write) - delay_samples + static_cast<float>(capture_.size),
            static_cast<float>(capture_.size));

        // Grain size with jitter (up to +/-25%)
        float size_jitter = 1.0f + rand_amt * 0.25f * rng_.next();
        float grain_ms = size_ms * std::fmax(0.25f, size_jitter);
        grain.length = std::max(1, static_cast<int>(grain_ms * 0.001f * sr));

        // Pitch with jitter (up to +/-1 semitone)
        float pitch_jitter = rand_amt * rng_.next(); // +/- 1 semitone
        float total_pitch = pitch_param + pitch_jitter;
        grain.playback_rate = std::pow(2.0f, total_pitch / 12.0f);

        grain.cursor = 0.0f;
        grain.window_type = win_type;
        grain.active = true;
    }

    void process_audio(const VividAudioContext* ctx) override {
        lazy_init(ctx->sample_rate);

        float* in  = ctx->input_buffers[0];
        float* out = ctx->output_buffers[0];
        uint32_t frames = ctx->buffer_size;

        float pos_cv = ctx->input_float_values ? ctx->input_float_values[0] : 0.0f;
        float pitch_cv = ctx->input_float_values ? ctx->input_float_values[1] : 0.0f;
        float density_cv = ctx->input_float_values ? ctx->input_float_values[2] : 0.0f;

        float mod_position = std::fmax(0.0f, std::fmin(1.0f, position.value + pos_cv));
        float mod_pitch    = std::fmax(-24.0f, std::fmin(24.0f, pitch.value + pitch_cv));
        float mod_density  = std::fmax(0.5f, std::fmin(60.0f, density.value + density_cv));

        float sr       = static_cast<float>(ctx->sample_rate);
        double inv_sr  = 1.0 / static_cast<double>(ctx->sample_rate);
        float wet      = mix.value;
        float dry      = 1.0f - wet;
        float size_ms  = grain_size.value;
        float rand_amt = randomize.value;
        int   win_type = window.int_value();

        for (uint32_t i = 0; i < frames; i++) {
            // 1. Push input into capture buffer
            capture_.push(in[i]);

            // 2. Advance scheduler
            double prev_phase = sched_phase_;
            sched_phase_ += mod_density * inv_sr;
            if (sched_phase_ >= 1.0) {
                sched_phase_ -= 1.0;
                spawn_grain(sr, mod_position, mod_pitch, size_ms, rand_amt, win_type);
            }

            // 3. Sum active grains
            float grain_sum = 0.0f;
            int active_count = 0;
            for (int g = 0; g < kMaxGrains; g++) {
                Grain& grain = grains_[g];
                if (!grain.active) continue;

                float phase = grain.cursor / static_cast<float>(grain.length);
                if (phase >= 1.0f) {
                    grain.active = false;
                    continue;
                }

                float env = grain_window(phase, grain.window_type);
                float read_pos = grain.start_pos + grain.cursor * grain.playback_rate;
                float sample = capture_.read_linear(read_pos);
                grain_sum += sample * env;
                active_count++;

                grain.cursor += 1.0f;
            }

            // 4. Normalize by 1/sqrt(active_count)
            if (active_count > 0) {
                grain_sum *= 1.0f / std::sqrt(static_cast<float>(active_count));
            }

            // 5. Mix
            out[i] = in[i] * dry + grain_sum * wet;
        }
    }
};

VIVID_REGISTER(GranularSynth)
