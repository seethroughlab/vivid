#include "operator_api/operator.h"

#include <cmath>
#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------
// Ping-Pong Delay — stereo delay with cross-feed, feedback filter, DC block
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

struct OnePoleFilter {
    float y1 = 0.0f;

    void reset() { y1 = 0.0f; }

    float lp(float x, float alpha) {
        y1 += alpha * (x - y1);
        return y1;
    }

    float hp(float x, float alpha) {
        return x - lp(x, alpha);
    }
};

static constexpr float kMaxDelaySeconds = 2.0f;

/**
 * @brief Stereo ping-pong delay with cross-feed and feedback filtering.
 *
 * Alternates echoes between left and right channels. The spread parameter
 * controls cross-feed between channels -- 0 is mono delay, 1 is full
 * ping-pong. Optional low/high pass filter on the feedback path.
 *
 * @param spread Cross-feed amount. 0 = mono, 1 = full stereo alternation.
 * @param filter Feedback filter mode. Tames harsh repeats or thins the tail.
 * @see Delay, Reverb
 */
struct PingPongDelay : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "PingPongDelay";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> time       {"time",        375.0f, 10.0f, 2000.0f};
    vivid::Param<float> feedback   {"feedback",      0.4f,  0.0f,    0.95f};
    vivid::Param<float> spread     {"spread",        1.0f,  0.0f,    1.0f};
    vivid::Param<int>   filter     {"filter",        0, {"Off", "LowPass", "HighPass"}};
    vivid::Param<float> filter_freq{"filter_freq", 3000.0f, 200.0f, 16000.0f};
    vivid::Param<float> mix        {"mix",           0.4f,  0.0f,    1.0f};

    DelayLine     delay_L_, delay_R_;
    OnePoleFilter filter_L_, filter_R_;
    float dc_x1_L_ = 0.0f, dc_y1_L_ = 0.0f;
    float dc_x1_R_ = 0.0f, dc_y1_R_ = 0.0f;
    bool     initialized_ = false;
    uint32_t init_rate_   = 0;

    PingPongDelay() {
        vivid::semantic_tag(time, "time_milliseconds");
        vivid::semantic_shape(time, "scalar");
        vivid::semantic_unit(time, "ms");
        vivid::display_hint(time, VIVID_DISPLAY_KNOB);
        vivid::description(time, "Delay time in milliseconds");

        vivid::semantic_tag(feedback, "probability_01");
        vivid::semantic_shape(feedback, "scalar");
        vivid::display_hint(feedback, VIVID_DISPLAY_KNOB);
        vivid::description(feedback, "Amount of delayed signal fed back into the delay line");

        vivid::semantic_tag(spread, "probability_01");
        vivid::semantic_shape(spread, "scalar");
        vivid::display_hint(spread, VIVID_DISPLAY_KNOB);
        vivid::description(spread, "Stereo cross-feed amount (0 = mono delay, 1 = full ping-pong)");

        vivid::semantic_tag(filter, "x_filter_mode");
        vivid::semantic_shape(filter, "enum");
        vivid::description(filter, "Filter applied to the feedback path: off, low-pass, or high-pass");

        vivid::semantic_tag(filter_freq, "frequency_hz");
        vivid::semantic_shape(filter_freq, "scalar");
        vivid::semantic_unit(filter_freq, "Hz");
        vivid::display_hint(filter_freq, VIVID_DISPLAY_KNOB);
        vivid::description(filter_freq, "Cutoff frequency of the feedback filter in Hz");

        vivid::semantic_tag(mix, "probability_01");
        vivid::semantic_shape(mix, "scalar");
        vivid::semantic_intent(mix, "wet_mix");
        vivid::display_hint(mix, VIVID_DISPLAY_KNOB);
        vivid::description(mix, "Blend between dry input and delayed signal");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&time);
        out.push_back(&feedback);
        out.push_back(&spread);
        out.push_back(&filter);
        out.push_back(&filter_freq);
        out.push_back(&mix);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",       VIVID_PORT_AUDIO, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f});
        out.push_back({"output",      VIVID_PORT_AUDIO, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f});
        out.push_back({"time_cv",     VIVID_PORT_SIGNAL, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        out.push_back({"feedback_cv", VIVID_PORT_SIGNAL, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        vivid::append_analysis_ports(out);
    }

    void lazy_init(uint32_t sr) {
        if (initialized_ && init_rate_ == sr) return;
        int max_samples = static_cast<int>(kMaxDelaySeconds * sr) + 1;
        delay_L_.init(max_samples);
        delay_R_.init(max_samples);
        filter_L_.reset();
        filter_R_.reset();
        dc_x1_L_ = dc_y1_L_ = 0.0f;
        dc_x1_R_ = dc_y1_R_ = 0.0f;
        initialized_ = true;
        init_rate_   = sr;
    }

    float dc_block_L(float x) {
        float y = x - dc_x1_L_ + 0.995f * dc_y1_L_;
        dc_x1_L_ = x;
        dc_y1_L_ = y;
        return y;
    }

    float dc_block_R(float x) {
        float y = x - dc_x1_R_ + 0.995f * dc_y1_R_;
        dc_x1_R_ = x;
        dc_y1_R_ = y;
        return y;
    }

    void process_audio(const VividAudioContext* ctx) override {
        lazy_init(ctx->sample_rate);

        const uint32_t frames = ctx->buffer_size;
        const float sr = static_cast<float>(ctx->sample_rate);

        // Stereo planar layout
        const float* L_in  = ctx->input_buffers[0];
        const float* R_in  = ctx->input_buffers[0] + frames;
        float* L_out = ctx->output_buffers[0];
        float* R_out = ctx->output_buffers[0] + frames;

        // CV offsets
        float time_cv_val = ctx->input_float_values ? ctx->input_float_values[0] : 0.0f;
        float fb_cv_val   = ctx->input_float_values ? ctx->input_float_values[1] : 0.0f;

        float t = time.value + time_cv_val;
        if (t < 10.0f)   t = 10.0f;
        if (t > 2000.0f) t = 2000.0f;

        int delay_samples = static_cast<int>(t * 0.001f * sr);
        if (delay_samples < 1) delay_samples = 1;
        if (delay_samples >= delay_L_.size) delay_samples = delay_L_.size - 1;

        float fb = feedback.value + fb_cv_val;
        if (fb < 0.0f)  fb = 0.0f;
        if (fb > 0.95f) fb = 0.95f;

        float sp  = spread.value;
        float wet = mix.value;
        float dry = 1.0f - wet;

        int filt_mode = filter.int_value();
        float alpha = 1.0f - std::exp(-2.0f * 3.14159265f * filter_freq.value / sr);

        for (uint32_t i = 0; i < frames; i++) {
            // Read delayed signals
            float del_L = delay_L_.read(delay_samples);
            float del_R = delay_R_.read(delay_samples);

            // Apply feedback filter
            if (filt_mode == 1) {       // LowPass
                del_L = filter_L_.lp(del_L, alpha);
                del_R = filter_R_.lp(del_R, alpha);
            } else if (filt_mode == 2) { // HighPass
                del_L = filter_L_.hp(del_L, alpha);
                del_R = filter_R_.hp(del_R, alpha);
            }

            // DC block
            del_L = dc_block_L(del_L);
            del_R = dc_block_R(del_R);

            // Cross-feed controlled by spread
            float fb_to_L = (del_L * (1.0f - sp) + del_R * sp) * fb;
            float fb_to_R = (del_R * (1.0f - sp) + del_L * sp) * fb;

            // Write input + feedback to delay lines
            delay_L_.push(L_in[i] + fb_to_L);
            delay_R_.push(R_in[i] + fb_to_R);

            // Wet/dry mix
            L_out[i] = L_in[i] * dry + del_L * wet;
            R_out[i] = R_in[i] * dry + del_R * wet;
        }
    }
};

VIVID_REGISTER(PingPongDelay)
