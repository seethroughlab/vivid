// Audio-rate Envelope variant. Uses Envelope types from envelope.h for lane state.
#include "envelope.h"

struct EnvelopeAu : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName = "envelope_au";
    static constexpr bool kTimeDependent = true;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_POINTWISE;
    static constexpr bool kStrategyIndependent = true;

    Envelope impl_;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        impl_.collect_params(out);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        impl_.collect_ports(out);
    }

    void process_audio(const VividAudioContext* ctx) override {
        Envelope::LaneState& s = ctx->lane_state_fn
            ? *vivid_lane_state(ctx, ctx->lane_id, Envelope::LaneState)
            : impl_.scalar_state_;

        const float sample_dt = 1.0f / static_cast<float>(ctx->sample_rate);

        impl_.advance_triggers(s, 0.0f, 0.0f);

        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            impl_.advance_adsr(s, sample_dt, impl_.attack.value, impl_.decay.value,
                               impl_.sustain.value, impl_.release.value, impl_.curve.int_value());
            ctx->output_buffers[0][i] = s.env_value * impl_.amplitude.value + impl_.offset.value;
        }
    }
};

VIVID_REGISTER(EnvelopeAu)
