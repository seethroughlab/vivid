#include "operator_api/operator.h"

#include <cmath>
#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------
// Freeverb-style algorithmic reverb (mono)
//
// DSP primitives inlined: CombFilter (feedback comb with lowpass damping)
// and AllPassDelay (Schroeder allpass for diffusion).
// ---------------------------------------------------------------------------

struct CombFilter {
    std::vector<float> buffer;
    int size = 0;
    int idx  = 0;
    float filterstore = 0.0f;

    void init(int len) {
        size = len;
        idx  = 0;
        filterstore = 0.0f;
        if (static_cast<int>(buffer.size()) < size) {
            buffer.assign(size, 0.0f);
        } else {
            std::fill_n(buffer.data(), size, 0.0f);
        }
    }

    float process(float input, float feedback, float damp1, float damp2) {
        float out = buffer[idx];
        filterstore = out * damp2 + filterstore * damp1;
        buffer[idx] = input + filterstore * feedback;
        if (++idx >= size) idx = 0;
        return out;
    }
};

struct AllPassDelay {
    std::vector<float> buffer;
    int size = 0;
    int idx  = 0;

    void init(int len) {
        size = len;
        idx  = 0;
        if (static_cast<int>(buffer.size()) < size) {
            buffer.assign(size, 0.0f);
        } else {
            std::fill_n(buffer.data(), size, 0.0f);
        }
    }

    float process(float input) {
        float bufout = buffer[idx];
        float out = bufout - input;
        buffer[idx] = input + bufout * 0.5f;
        if (++idx >= size) idx = 0;
        return out;
    }
};

// Freeverb comb delay lengths at 44100 Hz
static constexpr int kCombLengths[8]    = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
static constexpr int kAllPassLengths[4] = {556, 441, 341, 225};

/**
 * @brief Freeverb-style algorithmic reverb with room size and damping.
 *
 * Eight parallel comb filters feed into a cascade of four allpass filters,
 * producing a dense reverberant tail. Room size controls feedback (decay
 * length), damping controls high-frequency absorption.
 *
 * @see Delay, PingPongDelay
 */
struct Reverb : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "Reverb";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> room_size{"room_size", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> damping  {"damping",   0.5f, 0.0f, 1.0f};
    vivid::Param<float> mix      {"mix",       0.3f, 0.0f, 1.0f};

    CombFilter    combs[8];
    AllPassDelay  allpasses[4];
    bool          initialized_ = false;
    uint32_t      init_rate_   = 0;

    Reverb() {
        vivid::semantic_tag(room_size, "probability_01");
        vivid::semantic_shape(room_size, "scalar");
        vivid::description(room_size, "Size of the virtual space, controlling reverb decay length");

        vivid::semantic_tag(damping, "probability_01");
        vivid::semantic_shape(damping, "scalar");
        vivid::description(damping, "High-frequency absorption in the reverb tail (higher = darker)");

        vivid::semantic_tag(mix, "probability_01");
        vivid::semantic_shape(mix, "scalar");
        vivid::semantic_intent(mix, "wet_mix");
        vivid::description(mix, "Blend between dry input and reverb signal");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&room_size);
        out.push_back(&damping);
        out.push_back(&mix);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"output", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        vivid::append_analysis_ports(out);
    }

    void lazy_init(uint32_t sr) {
        if (initialized_ && init_rate_ == sr) return;
        double scale = static_cast<double>(sr) / 44100.0;
        for (int i = 0; i < 8; i++)
            combs[i].init(static_cast<int>(kCombLengths[i] * scale));
        for (int i = 0; i < 4; i++)
            allpasses[i].init(static_cast<int>(kAllPassLengths[i] * scale));
        initialized_ = true;
        init_rate_   = sr;
    }

    void process_audio(const VividAudioContext* ctx) override {
        lazy_init(ctx->sample_rate);

        float* in  = ctx->input_buffers[0];
        float* out = ctx->output_buffers[0];
        uint32_t frames = ctx->buffer_size;

        float fb    = room_size.value * 0.28f + 0.7f; // map 0-1 → 0.7-0.98
        float damp1 = damping.value;
        float damp2 = 1.0f - damp1;
        float wet   = mix.value;
        float dry   = 1.0f - wet;

        for (uint32_t i = 0; i < frames; i++) {
            float inp = in[i] * 0.125f; // scale by 1/8
            float sum = 0.0f;

            for (int c = 0; c < 8; c++)
                sum += combs[c].process(inp, fb, damp1, damp2);

            for (int a = 0; a < 4; a++)
                sum = allpasses[a].process(sum);

            out[i] = in[i] * dry + sum * wet;
        }
    }
};

VIVID_REGISTER(Reverb)
