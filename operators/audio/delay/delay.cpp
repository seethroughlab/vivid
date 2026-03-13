#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"

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

struct Delay : vivid::AudioOperatorBase {
    static constexpr const char* kName   = "Delay";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> time    {"time",     250.0f, 0.0f, 2000.0f};
    vivid::Param<float> feedback{"feedback",   0.3f, 0.0f,    0.99f};
    vivid::Param<float> mix     {"mix",        0.5f, 0.0f,    1.0f};

    DelayLine delay_;
    bool      initialized_ = false;
    uint32_t  init_rate_   = 0;

    // DC blocker state
    float dc_x1_ = 0.0f;
    float dc_y1_ = 0.0f;

    Delay() {
        vivid::semantic_tag(time, "time_milliseconds");
        vivid::semantic_shape(time, "scalar");
        vivid::semantic_unit(time, "ms");

        vivid::semantic_tag(feedback, "probability_01");
        vivid::semantic_shape(feedback, "scalar");

        vivid::semantic_tag(mix, "probability_01");
        vivid::semantic_shape(mix, "scalar");
        vivid::semantic_intent(mix, "wet_mix");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&time);
        out.push_back(&feedback);
        out.push_back(&mix);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_AUDIO, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"output", VIVID_PORT_AUDIO, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
    }

    void lazy_init(uint32_t sr) {
        if (initialized_ && init_rate_ == sr) return;
        int max_samples = static_cast<int>(kMaxDelaySeconds * sr) + 1;
        delay_.init(max_samples);
        dc_x1_ = 0.0f;
        dc_y1_ = 0.0f;
        initialized_ = true;
        init_rate_   = sr;
    }

    float dc_block(float x) {
        // y[n] = x[n] - x[n-1] + 0.995 * y[n-1]
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

        int delay_samples = static_cast<int>(time.value * 0.001f * ctx->sample_rate);
        if (delay_samples < 1) delay_samples = 1;
        if (delay_samples >= delay_.size) delay_samples = delay_.size - 1;

        float fb  = feedback.value;
        float wet = mix.value;
        float dry = 1.0f - wet;

        for (uint32_t i = 0; i < frames; i++) {
            float delayed = delay_.read(delay_samples);
            float fb_signal = dc_block(delayed);
            delay_.push(in[i] + fb_signal * fb);
            out[i] = in[i] * dry + delayed * wet;
        }
    }
};

VIVID_REGISTER(Delay)
