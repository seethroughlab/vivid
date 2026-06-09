#include "operator_api/operator.h"

#include <cmath>

/**
 * @brief Bit-depth reduction and sample-rate decimation effect.
 *
 * Combines two lo-fi effects: **bit crushing** (quantizing amplitude to
 * fewer levels) and **sample-rate reduction** (sample-and-hold decimation).
 * Together they produce the characteristic "retro digital" sound of early
 * samplers and video game consoles.
 *
 * Processes up to stereo, per-channel, so it preserves the stereo image of
 * whatever feeds it (each channel keeps its own sample-and-hold state).
 *
 * @tip Automate `bits` with an LFO for evolving texture. Values below 4 get aggressive.
 * @tip Set rate to match a classic sampler (8000 Hz = telephone, 22050 Hz = early CD-ROM).
 * @see Distortion, Filter, RingMod
 * @param bits Number of quantization levels (2^bits). Lower = crunchier.
 * @param rate Target sample rate for decimation. Lower = more aliasing artifacts.
 * @param mix Dry/wet blend. 0 = bypass, 1 = fully crushed.
 * @input input Audio signal to process (mono or stereo).
 * @output output The crushed signal (same channel count as the input).
 */
struct Bitcrush : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "Bitcrush";
    static constexpr bool kTimeDependent = false;
    static constexpr bool kStrategyIndependent = true;

    static constexpr uint32_t kMaxChannels = 2;

    vivid::Param<float> bits{"bits",  8.0f, 1.0f, 16.0f};
    vivid::Param<float> rate{"rate",  8000.0f, 100.0f, 48000.0f};
    vivid::Param<float> mix {"mix",   1.0f, 0.0f, 1.0f};

    // Per-channel sample-and-hold state (decimation must be independent per
    // channel or the stereo image collapses to dual-mono).
    struct LaneState {
        float hold[kMaxChannels]    = {0.0f, 0.0f};
        float counter[kMaxChannels] = {0.0f, 0.0f};
    };

    Bitcrush() {
        vivid::semantic_tag(bits, "count");
        vivid::semantic_shape(bits, "scalar");
        vivid::semantic_intent(bits, "bit_depth");
        vivid::description(bits, "Bit depth for amplitude quantization (lower = crunchier)");

        vivid::semantic_tag(rate, "frequency_hz");
        vivid::semantic_shape(rate, "scalar");
        vivid::semantic_unit(rate, "Hz");
        vivid::description(rate, "Target sample rate for decimation in Hz");

        vivid::semantic_tag(mix, "probability_01");
        vivid::semantic_shape(mix, "scalar");
        vivid::semantic_intent(mix, "wet_mix");
        vivid::description(mix, "Dry/wet blend (0 = bypass, 1 = fully crushed)");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&bits);
        out.push_back(&rate);
        out.push_back(&mix);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f});
        out.push_back({"output", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f});
        vivid::append_analysis_ports(out);
    }

    void process_audio(const VividAudioContext* ctx) override {
        auto& ls = *vivid_lane_state(ctx, ctx->lane_id, LaneState);

        const uint32_t frames = ctx->buffer_size;
        uint32_t nch = ctx->input_channel_counts ? ctx->input_channel_counts[0] : 1u;
        if (nch > kMaxChannels) nch = kMaxChannels;

        float target_rate = rate.value;
        float sr = static_cast<float>(ctx->sample_rate);
        float ratio = sr / target_rate;
        if (ratio < 1.0f) ratio = 1.0f;

        const float levels = std::pow(2.0f, bits.value);
        const float wet = mix.value;
        const float dry = 1.0f - wet;

        // Planar layout: channel c occupies [c*frames, (c+1)*frames). Each
        // channel runs its own sample-and-hold so the stereo image survives.
        for (uint32_t c = 0; c < nch; c++) {
            const float* in  = ctx->input_buffers[0]  + c * frames;
            float*       out = ctx->output_buffers[0] + c * frames;
            for (uint32_t i = 0; i < frames; i++) {
                ls.counter[c] += 1.0f;
                if (ls.counter[c] >= ratio) {
                    ls.counter[c] -= ratio;
                    // Sample-and-hold: grab new value and quantize
                    float v = in[i];
                    v = std::round(v * levels) / levels;
                    ls.hold[c] = v;
                }
                out[i] = in[i] * dry + ls.hold[c] * wet;
            }
        }
    }
};

VIVID_DEFINE_OP(Bitcrush) {
}

