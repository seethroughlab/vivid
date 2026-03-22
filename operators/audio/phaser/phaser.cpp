#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "operator_api/audio_dsp.h"

#include <cmath>
#include <vector>

// ---------------------------------------------------------------------------
// Phaser — chain of first-order tunable allpass filters swept by LFO (mono)
// ---------------------------------------------------------------------------

struct AllPass1 {
    float x1 = 0.0f;
    float y1 = 0.0f;

    void reset() { x1 = 0.0f; y1 = 0.0f; }

    float process(float x, float g) {
        float y = -g * x + x1 + g * y1;
        x1 = x;
        y1 = y;
        return y;
    }
};

static constexpr int   kMaxStages = 12;
static constexpr float kMinFreq   = 100.0f;
static constexpr float kMaxFreq   = 4000.0f;

struct Phaser : vivid::AudioOperatorBase {
    static constexpr const char* kName   = "Phaser";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> rate    {"rate",     0.3f, 0.01f, 10.0f};
    vivid::Param<float> depth   {"depth",    0.7f, 0.0f,   1.0f};
    vivid::Param<int>   stages  {"stages",   1, {"2", "4", "6", "8", "10", "12"}};
    vivid::Param<float> feedback{"feedback", 0.3f, 0.0f,   0.95f};
    vivid::Param<float> mix     {"mix",      0.5f, 0.0f,   1.0f};

    AllPass1 allpasses_[kMaxStages];
    double   phase_       = 0.0;
    float    fb_state_    = 0.0f;
    bool     initialized_ = false;
    uint32_t init_rate_   = 0;

    Phaser() {
        vivid::semantic_tag(rate, "frequency_hz");
        vivid::semantic_shape(rate, "scalar");
        vivid::semantic_unit(rate, "Hz");
        vivid::display_hint(rate, VIVID_DISPLAY_KNOB);

        vivid::semantic_tag(depth, "probability_01");
        vivid::semantic_shape(depth, "scalar");
        vivid::display_hint(depth, VIVID_DISPLAY_KNOB);

        vivid::semantic_shape(stages, "scalar");
        vivid::semantic_intent(stages, "stage_count");

        vivid::semantic_tag(feedback, "probability_01");
        vivid::semantic_shape(feedback, "scalar");
        vivid::display_hint(feedback, VIVID_DISPLAY_KNOB);

        vivid::semantic_tag(mix, "probability_01");
        vivid::semantic_shape(mix, "scalar");
        vivid::semantic_intent(mix, "wet_mix");
        vivid::display_hint(mix, VIVID_DISPLAY_KNOB);
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&rate);
        out.push_back(&depth);
        out.push_back(&stages);
        out.push_back(&feedback);
        out.push_back(&mix);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",   VIVID_PORT_AUDIO, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"output",  VIVID_PORT_AUDIO, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"rate_cv", VIVID_PORT_SIGNAL, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
    }

    void lazy_init(uint32_t sr) {
        if (initialized_ && init_rate_ == sr) return;
        for (int i = 0; i < kMaxStages; i++)
            allpasses_[i].reset();
        phase_    = 0.0;
        fb_state_ = 0.0f;
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
        if (mod_rate < 0.01f) mod_rate = 0.01f;
        if (mod_rate > 10.0f) mod_rate = 10.0f;

        int stage_count = (stages.int_value() + 1) * 2;  // enum index 0→2, 1→4, ... 5→12
        float fb  = feedback.value;
        float wet = mix.value;
        float dry = 1.0f - wet;
        float d   = depth.value;
        float sr  = static_cast<float>(ctx->sample_rate);
        double inv_sr = 1.0 / static_cast<double>(ctx->sample_rate);

        for (uint32_t i = 0; i < frames; i++) {
            // Sine LFO mapped to [0,1]
            float lfo = static_cast<float>(audio_dsp::waveform(phase_, 0)) * 0.5f + 0.5f;
            phase_ += mod_rate * inv_sr;
            if (phase_ >= 1.0) phase_ -= 1.0;

            // Map LFO to sweep frequency range scaled by depth
            float sweep_freq = kMinFreq + (kMaxFreq - kMinFreq) * lfo * d;

            // Compute allpass coefficient
            float tan_val = std::tan(static_cast<float>(M_PI) * sweep_freq / sr);
            float g = (tan_val - 1.0f) / (tan_val + 1.0f);

            // Feed input with feedback
            float x = in[i] + fb_state_ * fb;

            // Run through allpass cascade
            float ap_out = x;
            for (int s = 0; s < stage_count; s++)
                ap_out = allpasses_[s].process(ap_out, g);

            fb_state_ = ap_out;
            out[i] = in[i] * dry + ap_out * wet;
        }
    }
};

VIVID_REGISTER(Phaser)
