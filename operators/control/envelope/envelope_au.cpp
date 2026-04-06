// Audio-rate Envelope variant.
#include "envelope.h"
#include "control/audio_scalar_utils.h"
#include "operator_api/thumbnail.h"

struct EnvelopeAu : Envelope, vivid::AudioProcessable {
    static constexpr const char* kName = "EnvelopeAu";

    void process_audio(const VividAudioContext* ctx) override {
        LaneState& s = ctx->lane_state_fn
            ? *vivid_lane_state(ctx, ctx->lane_id, LaneState)
            : scalar_state_;

        const float sample_dt = 1.0f / static_cast<float>(ctx->sample_rate);
        const vivid::MetronomeTransport metronome = vivid::metronome_transport(ctx);

        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            float gate_in = vivid::audio_scalar_sample(ctx, 0, i);
            vivid::MetronomeTransport sample_metronome =
                vivid::metronome_transport_sample(metronome, i, ctx->sample_rate);
            float phase_in = vivid::resolve_clock_phase(
                clock_source.int_value(), vivid::audio_scalar_sample(ctx, 1, i), sample_metronome);
            advance_triggers(s, gate_in, phase_in);
            advance_adsr(s, sample_dt, attack.value, decay.value,
                         sustain.value, release.value, curve.int_value());
            ctx->output_buffers[0][i] = s.env_value * amplitude.value + offset.value;
        }
    }
};

VIVID_REGISTER(EnvelopeAu)
VIVID_THUMBNAIL(EnvelopeAu)
VIVID_INSPECTOR(EnvelopeAu)
