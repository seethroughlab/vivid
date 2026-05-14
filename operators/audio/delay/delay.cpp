#include "operator_api/operator.h"

#include <cmath>
#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------
// Delay with feedback and DC-blocking (mono)
// ---------------------------------------------------------------------------

struct DelayLine {
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

    float read(int delay_samples) const {
        int idx = write - delay_samples;
        if (idx < 0) idx += size;
        return buffer[idx];
    }
};

static constexpr float kMaxDelaySeconds = 2.0f;

/**
 * @brief Mono feedback delay line with up to 2 seconds of delay.
 *
 * Classic echo effect with adjustable delay time, feedback, and dry/wet
 * mix. Includes DC blocking on the feedback path to prevent buildup.
 *
 * @see PingPongDelay, Chorus, Reverb
 */
struct Delay : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "Delay";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> time    {"time",     250.0f, 0.0f, 2000.0f};
    vivid::Param<float> feedback{"feedback",   0.3f, 0.0f,    0.99f};
    vivid::Param<float> mix     {"mix",        0.5f, 0.0f,    1.0f};

    static constexpr uint32_t kMaxChannels = 2;
    DelayLine delay_[kMaxChannels];
    bool      initialized_ = false;
    uint32_t  init_rate_   = 0;

    // DC blocker state (per channel)
    float dc_x1_[kMaxChannels] = {};
    float dc_y1_[kMaxChannels] = {};

    Delay() {
        vivid::semantic_tag(time, "time_milliseconds");
        vivid::semantic_shape(time, "scalar");
        vivid::semantic_unit(time, "ms");
        vivid::description(time, "Delay time in milliseconds (up to 2 seconds)");

        vivid::semantic_tag(feedback, "probability_01");
        vivid::semantic_shape(feedback, "scalar");
        vivid::description(feedback, "Amount of delayed signal fed back into the delay line");

        vivid::semantic_tag(mix, "probability_01");
        vivid::semantic_shape(mix, "scalar");
        vivid::semantic_intent(mix, "wet_mix");
        vivid::description(mix, "Dry/wet blend (0 = dry, 1 = fully delayed)");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&time);
        out.push_back(&feedback);
        out.push_back(&mix);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 0, 0.0f});
        out.push_back({"output", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 0, 0.0f});
        vivid::append_analysis_ports(out);
    }

    void lazy_init(uint32_t sr) {
        if (initialized_ && init_rate_ == sr) return;
        int max_samples = static_cast<int>(kMaxDelaySeconds * sr) + 1;
        for (uint32_t c = 0; c < kMaxChannels; c++) {
            delay_[c].init(max_samples);
            dc_x1_[c] = 0.0f;
            dc_y1_[c] = 0.0f;
        }
        initialized_ = true;
        init_rate_   = sr;
    }

    void process_audio(const VividAudioContext* ctx) override {
        lazy_init(ctx->sample_rate);

        uint32_t nch = ctx->input_channel_counts ? ctx->input_channel_counts[0] : 1u;
        if (nch > kMaxChannels) nch = kMaxChannels;
        uint32_t frames = ctx->buffer_size;

        int delay_samples = static_cast<int>(time.value * 0.001f * ctx->sample_rate);
        if (delay_samples < 1) delay_samples = 1;
        if (delay_samples >= delay_[0].size) delay_samples = delay_[0].size - 1;

        float fb  = feedback.value;
        float wet = mix.value;
        float dry = 1.0f - wet;

        for (uint32_t c = 0; c < nch; c++) {
            const float* in_c  = ctx->input_buffers[0]  + c * frames;
            float*       out_c = ctx->output_buffers[0] + c * frames;
            for (uint32_t i = 0; i < frames; i++) {
                float delayed = delay_[c].read(delay_samples);
                float y = delayed - dc_x1_[c] + 0.995f * dc_y1_[c];
                dc_x1_[c] = delayed;
                dc_y1_[c] = y;
                delay_[c].push(in_c[i] + y * fb);
                out_c[i] = in_c[i] * dry + delayed * wet;
            }
        }
    }
};

VIVID_DEFINE_OP(Delay) {
}

