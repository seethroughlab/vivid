#include "operator_api/operator.h"

#include <cmath>
#include <vector>

/**
 * @brief Brickwall lookahead limiter with smooth release.
 *
 * Prevents the signal from exceeding the ceiling level using lookahead
 * peak detection for transparent limiting. Attack is instant; release
 * smoothly returns to unity gain.
 *
 * @param ceiling Maximum output level in dB. -0.3 dB is a safe default.
 * @param lookahead Anticipation time in ms. Longer catches faster transients but adds latency.
 * @see Compressor, Gain
 */
struct Limiter : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "Limiter";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> ceiling  {"ceiling",    -0.3f, -20.0f,    0.0f};
    vivid::Param<float> release  {"release",   100.0f,  10.0f, 1000.0f};
    vivid::Param<float> lookahead{"lookahead",   5.0f,   1.0f,    5.0f};

    static constexpr uint32_t kMaxChannels = 2;
    std::vector<float> delay_buf_[kMaxChannels];
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
        vivid::description(ceiling, "Maximum output level in dB; signal peaks are clamped here");

        vivid::semantic_tag(release, "time_milliseconds");
        vivid::semantic_shape(release, "scalar");
        vivid::semantic_unit(release, "ms");
        vivid::display_hint(release, VIVID_DISPLAY_KNOB);
        vivid::description(release, "How quickly gain returns to unity after limiting, in milliseconds");

        vivid::semantic_tag(lookahead, "time_milliseconds");
        vivid::semantic_shape(lookahead, "scalar");
        vivid::semantic_unit(lookahead, "ms");
        vivid::display_hint(lookahead, VIVID_DISPLAY_KNOB);
        vivid::description(lookahead, "Anticipation time in ms; longer values catch faster transients but add latency");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&ceiling);
        out.push_back(&release);
        out.push_back(&lookahead);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 0, 0.0f});
        out.push_back({"output", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 0, 0.0f});
        vivid::append_analysis_ports(out);
    }

    void lazy_init(uint32_t sr) {
        if (initialized_ && init_rate_ == sr) return;
        int max_samples = static_cast<int>(0.005f * sr) + 1;
        delay_size_  = max_samples;
        delay_write_ = 0;
        for (uint32_t c = 0; c < kMaxChannels; c++)
            delay_buf_[c].assign(max_samples, 0.0f);
        env_ = 0.0f;
        initialized_ = true;
        init_rate_   = sr;
    }

    void process_audio(const VividAudioContext* ctx) override {
        lazy_init(ctx->sample_rate);

        uint32_t nch = ctx->input_channel_counts ? ctx->input_channel_counts[0] : 1u;
        if (nch > kMaxChannels) nch = kMaxChannels;
        uint32_t frames = ctx->buffer_size;

        float sr = static_cast<float>(ctx->sample_rate);
        float ceiling_linear = std::pow(10.0f, ceiling.value / 20.0f);
        float rel_coeff = std::exp(-1.0f / (release.value * 0.001f * sr));

        int delay_samples = static_cast<int>(lookahead.value * 0.001f * sr);
        if (delay_samples < 1) delay_samples = 1;
        if (delay_samples >= delay_size_) delay_samples = delay_size_ - 1;

        float env = env_;

        for (uint32_t i = 0; i < frames; i++) {
            int rd = delay_write_ - delay_samples;
            if (rd < 0) rd += delay_size_;

            // Peak detect from max(|L|, |R|) on the pre-delay signal
            float peak = 0.0f;
            for (uint32_t c = 0; c < nch; c++) {
                float a = std::fabs(ctx->input_buffers[0][c * frames + i]);
                if (a > peak) peak = a;
            }

            float desired_gain = (peak > ceiling_linear) ? ceiling_linear / peak : 1.0f;
            if (desired_gain < env)
                env = desired_gain;
            else
                env = rel_coeff * env + (1.0f - rel_coeff) * desired_gain;

            // Apply same gain to all channels from their respective delay buffers
            for (uint32_t c = 0; c < nch; c++) {
                const float* in_c  = ctx->input_buffers[0]  + c * frames;
                float*       out_c = ctx->output_buffers[0] + c * frames;
                float delayed = delay_buf_[c][rd];
                delay_buf_[c][delay_write_] = in_c[i];
                float result = delayed * env;
                if (result >  ceiling_linear) result =  ceiling_linear;
                if (result < -ceiling_linear) result = -ceiling_linear;
                out_c[i] = result;
            }

            if (++delay_write_ >= delay_size_) delay_write_ = 0;
        }

        env_ = env;
    }
};

VIVID_DEFINE_OP(Limiter) {
}

