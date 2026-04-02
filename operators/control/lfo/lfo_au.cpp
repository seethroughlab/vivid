// Audio-rate LFO variant. Uses LFO types from lfo.h for lane state.
#include "lfo.h"

struct LfoAu : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName = "lfo_au";
    static constexpr bool kTimeDependent = true;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_POINTWISE;
    static constexpr bool kStrategyIndependent = true;

    // Reuse LFO's params, ports, and compute logic.
    LFO impl_;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        impl_.collect_params(out);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        impl_.collect_ports(out);
    }

    void process_audio(const VividAudioContext* ctx) override {
        LFO::LaneState& s = ctx->lane_state_fn
            ? *vivid_lane_state(ctx, ctx->lane_id, LFO::LaneState)
            : impl_.scalar_state_;

        const double sample_dt = 1.0 / static_cast<double>(ctx->sample_rate);

        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            ctx->output_buffers[0][i] = impl_.compute_one_sample(
                s, impl_.frequency.value, impl_.amplitude.value, impl_.offset.value,
                impl_.waveform.int_value(), impl_.rate_mode.int_value(),
                impl_.polarity.int_value(), impl_.distribution.int_value(),
                impl_.seed.int_value(), static_cast<float>(impl_.phase_offset.value),
                impl_.fade_in.value,
                ctx->input_buffers[0][i], ctx->input_buffers[1][i],
                sample_dt, impl_.slew.value);
        }
    }
};

VIVID_REGISTER(LfoAu)
