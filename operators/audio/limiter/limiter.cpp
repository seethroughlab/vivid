#include "operator_api/operator.h"

#include <cmath>
#include <vector>

// ---------------------------------------------------------------------------
// Brickwall lookahead limiter for peak protection
// ---------------------------------------------------------------------------

struct Limiter : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "Limiter";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> ceiling  {"ceiling",    -0.3f, -20.0f,    0.0f};
    vivid::Param<float> release  {"release",   100.0f,  10.0f, 1000.0f};
    vivid::Param<float> lookahead{"lookahead",   5.0f,   1.0f,    5.0f};

    std::vector<float> delay_buf_;
    int    delay_size_    = 0;
    int    delay_write_   = 0;
    float  env_           = 0.0f;
    bool   initialized_   = false;
    uint32_t init_rate_   = 0;

    Limiter() {
        vivid::semantic_tag(ceiling, "gain_db");
        vivid::semantic_shape(ceiling, "scalar");
        vivid::semantic_unit(ceiling, "dB");
        vivid::display_hint(ceiling, VIVID_DISPLAY_KNOB);

        vivid::semantic_tag(release, "time_milliseconds");
        vivid::semantic_shape(release, "scalar");
        vivid::semantic_unit(release, "ms");
        vivid::display_hint(release, VIVID_DISPLAY_KNOB);

        vivid::semantic_tag(lookahead, "time_milliseconds");
        vivid::semantic_shape(lookahead, "scalar");
        vivid::semantic_unit(lookahead, "ms");
        vivid::display_hint(lookahead, VIVID_DISPLAY_KNOB);
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&ceiling);
        out.push_back(&release);
        out.push_back(&lookahead);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_AUDIO, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"output", VIVID_PORT_AUDIO, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
    }

    void lazy_init(uint32_t sr) {
        if (initialized_ && init_rate_ == sr) return;
        // Max lookahead is 5ms
        int max_samples = static_cast<int>(0.005f * sr) + 1;
        delay_size_  = max_samples;
        delay_write_ = 0;
        delay_buf_.assign(max_samples, 0.0f);
        env_ = 0.0f;
        initialized_ = true;
        init_rate_   = sr;
    }

    void process_audio(const VividAudioContext* ctx) override {
        lazy_init(ctx->sample_rate);

        float* in  = ctx->input_buffers[0];
        float* out = ctx->output_buffers[0];
        uint32_t frames = ctx->buffer_size;

        float sr = static_cast<float>(ctx->sample_rate);
        float ceiling_linear = std::pow(10.0f, ceiling.value / 20.0f);
        float rel_coeff = std::exp(-1.0f / (release.value * 0.001f * sr));

        int delay_samples = static_cast<int>(lookahead.value * 0.001f * sr);
        if (delay_samples < 1) delay_samples = 1;
        if (delay_samples >= delay_size_) delay_samples = delay_size_ - 1;

        float env = env_;

        for (uint32_t i = 0; i < frames; i++) {
            // Read delayed sample
            int rd = delay_write_ - delay_samples;
            if (rd < 0) rd += delay_size_;
            float delayed = delay_buf_[rd];

            // Write current sample into delay line
            delay_buf_[delay_write_] = in[i];
            if (++delay_write_ >= delay_size_) delay_write_ = 0;

            // Peak detection on the incoming (non-delayed) signal
            float peak = std::fabs(in[i]);

            // Compute desired gain reduction
            float desired_gain = (peak > ceiling_linear)
                ? ceiling_linear / peak
                : 1.0f;

            // Envelope: instant attack, smooth release
            if (desired_gain < env)
                env = desired_gain;  // Instant attack
            else
                env = rel_coeff * env + (1.0f - rel_coeff) * desired_gain;

            // Apply gain to the delayed signal
            float result = delayed * env;

            // Brickwall safety clamp
            if (result >  ceiling_linear) result =  ceiling_linear;
            if (result < -ceiling_linear) result = -ceiling_linear;

            out[i] = result;
        }

        env_ = env;
    }
};

VIVID_REGISTER(Limiter)
