#include "operator_api/operator.h"
#include "shared/reverb_dsp/reverb_dsp.h"

// ---------------------------------------------------------------------------
// Freeverb-style algorithmic reverb (mono)
// ---------------------------------------------------------------------------

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

    vivid::reverb_dsp::Engine engine_;

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

    void process_audio(const VividAudioContext* ctx) override {
        const float* in = ctx->input_buffers[0];
        float* out = ctx->output_buffers[0];

        vivid::reverb_dsp::ProcessParams params{};
        params.room_size = room_size.value;
        params.damping = damping.value;
        params.mix = mix.value;
        engine_.process(in, out, ctx->buffer_size, ctx->sample_rate, params);
    }
};

VIVID_REGISTER(Reverb)
