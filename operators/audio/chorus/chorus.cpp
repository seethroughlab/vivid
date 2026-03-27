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

struct Chorus : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "Chorus";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> rate  {"rate",   0.5f, 0.05f, 5.0f};
    vivid::Param<float> depth {"depth",  0.5f, 0.0f,  1.0f};
    vivid::Param<int>   voices{"voices", 3, 1, 6};
    vivid::Param<float> mix   {"mix",    0.5f, 0.0f,  1.0f};

    FractionalDelayLine delays_[kMaxVoices];
    double   phase_       = 0.0;
    bool     initialized_ = false;
    uint32_t init_rate_   = 0;

    Chorus() {
        vivid::semantic_tag(rate, "frequency_hz");
        vivid::semantic_shape(rate, "scalar");
        vivid::semantic_unit(rate, "Hz");
        vivid::display_hint(rate, VIVID_DISPLAY_KNOB);

        vivid::semantic_tag(depth, "probability_01");
        vivid::semantic_shape(depth, "scalar");
        vivid::display_hint(depth, VIVID_DISPLAY_KNOB);

        vivid::semantic_tag(voices, "count");
        vivid::semantic_shape(voices, "scalar");
        vivid::display_hint(voices, VIVID_DISPLAY_KNOB);

        vivid::semantic_tag(mix, "probability_01");
        vivid::semantic_shape(mix, "scalar");
        vivid::semantic_intent(mix, "wet_mix");
        vivid::display_hint(mix, VIVID_DISPLAY_KNOB);
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&rate);
        out.push_back(&depth);
        out.push_back(&voices);
        out.push_back(&mix);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",   VIVID_PORT_AUDIO, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"output",  VIVID_PORT_AUDIO, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"rate_cv", VIVID_PORT_SIGNAL, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
    }

    void lazy_init(uint32_t sr) {
        if (initialized_ && init_rate_ == sr) return;
        int max_samples = static_cast<int>(kMaxDelay * sr) + 2;
        for (int v = 0; v < kMaxVoices; v++)
            delays_[v].init(max_samples);
        phase_ = 0.0;
        initialized_ = true;
        init_rate_   = sr;
    }

    void process_audio(const VividAudioContext* ctx) override {
        lazy_init(ctx->sample_rate);

        float* in  = ctx->input_buffers[0];
        float* out = ctx->output_buffers[0];
        uint32_t frames = ctx->buffer_size;

        float rate_cv_val = ctx->input_float_values ? ctx->input_float_values[0] : 0.0f;
        float mod_rate = rate.value + rate_cv_val;
        if (mod_rate < 0.05f) mod_rate = 0.05f;
        if (mod_rate > 5.0f)  mod_rate = 5.0f;

        int   voice_count = voices.int_value();
        float wet = mix.value;
        float dry = 1.0f - wet;
        float d   = depth.value;
        float sr  = static_cast<float>(ctx->sample_rate);
        double inv_sr = 1.0 / static_cast<double>(ctx->sample_rate);

        float center_samples = kCenterDelay * sr;
        float depth_samples  = kMaxDepth * d * sr;

        for (uint32_t i = 0; i < frames; i++) {
            // Write input to all delay lines (for seamless voice-count changes)
            for (int v = 0; v < kMaxVoices; v++)
                delays_[v].push(in[i]);

            float sum = 0.0f;
            for (int v = 0; v < voice_count; v++) {
                double voice_phase = phase_ + static_cast<double>(v) / static_cast<double>(voice_count);
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

            phase_ += mod_rate * inv_sr;
            if (phase_ >= 1.0) phase_ -= 1.0;
        }
    }
};

VIVID_REGISTER(Chorus)
