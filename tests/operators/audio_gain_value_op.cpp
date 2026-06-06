// Value-API audio example operator (lane-value clean-break, Phase 5a).
//
// Scales its audio input by `gain` using the value-model API: reads the audio
// block via ctx->values[0] (VIVID_VALUE_AUDIO) and writes the output via
// ctx->value_outputs[0]->resize (the runtime-provided block) instead of
// ctx->input_buffers / output_buffers. Exercises the AUDIO value path through the
// real (Scalar) audio executor. Declares Map multiplicity behavior.
#include "operator_api/operator.h"

struct AudioGainValueOp : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "AudioGainValueOp";
    static constexpr bool kTimeDependent = false;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_POINTWISE;
    static constexpr VividMultiplicityBehavior kMultiplicityBehavior = VIVID_MULTIPLICITY_MAP;

    vivid::Param<float> gain{"gain", 1.0f, 0.0f, 4.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override { out.push_back(&gain); }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT});
        out.push_back({"output", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT});
    }

    void process_audio(const VividAudioContext* ctx) override {
        const uint32_t frames = ctx->buffer_size;
        uint32_t nch = ctx->output_channel_counts ? ctx->output_channel_counts[0] : 1u;
        if (nch > 2u) nch = 2u;
        const float g = gain.value;

        // Read the audio input via the value API.
        const float* in = ctx->values ? vivid_value_audio(&ctx->values[0]) : nullptr;
        // Write the output via the value API (resize → the runtime audio block).
        float* out = nullptr;
        if (ctx->value_outputs && ctx->value_outputs[0].resize)
            out = static_cast<float*>(ctx->value_outputs[0].resize(ctx->value_outputs[0].handle, 1));

        if (!in || !out) return;
        for (uint32_t c = 0; c < nch; ++c) {
            const float* ic = in  + c * frames;
            float*       oc = out + c * frames;
            for (uint32_t i = 0; i < frames; ++i) oc[i] = ic[i] * g;
        }
        if (ctx->value_outputs && ctx->value_outputs[0].commit)
            ctx->value_outputs[0].commit(ctx->value_outputs[0].handle, 1);
    }
};
