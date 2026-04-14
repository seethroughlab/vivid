// Audio-rate Envelope variant with polyphonic (lane-array) gate support.
#include "envelope.h"
#include "control/audio_scalar_utils.h"
#include "operator_api/thumbnail.h"

#include <cstring>

struct EnvelopeAudio : Envelope, vivid::AudioProcessable {
    static constexpr const char* kName = "Envelope";

    void process_audio(const VividAudioContext* ctx) override {
        uint32_t frames = ctx->buffer_size;
        float sample_dt = 1.0f / static_cast<float>(ctx->sample_rate);
        const vivid::MetronomeTransport metronome = vivid::metronome_transport(ctx);

        // Read lane arrays for gate and lane_ids (ports 0, 1)
        const VividLaneView* gate_lane = ctx->input_lanes ? &ctx->input_lanes[0] : nullptr;
        const VividLaneView* lane_id_lane = ctx->input_lanes ? &ctx->input_lanes[1] : nullptr;
        uint32_t voice_count = (gate_lane && gate_lane->data) ? gate_lane->length : 0;

        // Fallback: scalar (non-polyphonic) mode
        if (voice_count == 0) {
            LaneState& s = ctx->lane_state_fn
                ? *vivid_lane_state(ctx, ctx->lane_id, LaneState)
                : scalar_state_;
            for (uint32_t i = 0; i < frames; ++i) {
                float gate_in = vivid::audio_scalar_sample(ctx, 0, i);
                vivid::MetronomeTransport sample_metronome =
                    vivid::metronome_transport_sample(metronome, i, ctx->sample_rate);
                float phase_in = vivid::resolve_clock_phase(
                    clock_source.int_value(), vivid::audio_scalar_sample(ctx, 2, i), sample_metronome);
                advance_triggers(s, gate_in, phase_in);
                advance_adsr(s, sample_dt, attack.value, decay.value,
                             sustain.value, release.value, curve.int_value());
                ctx->output_buffers[0][i] = s.env_value * amplitude.value + offset.value;
            }
            return;
        }

        // Polyphonic: one output channel per voice
        if (voice_count > kMaxVoices) voice_count = kMaxVoices;

        // Zero entire output buffer so unused channels are silent
        std::memset(ctx->output_buffers[0], 0, kMaxVoices * frames * sizeof(float));

        for (uint32_t vi = 0; vi < voice_count; ++vi) {
            float gate_val = gate_lane->data[vi];
            uint32_t lid = (lane_id_lane && lane_id_lane->data && vi < lane_id_lane->length)
                ? static_cast<uint32_t>(lane_id_lane->data[vi]) : vi;

            LaneState& s = ctx->lane_state_fn
                ? *vivid_lane_state(ctx, lid, LaneState)
                : scalar_state_;

            float* out_ch = ctx->output_buffers[0] + vi * frames;

            for (uint32_t i = 0; i < frames; ++i) {
                vivid::MetronomeTransport sample_metronome =
                    vivid::metronome_transport_sample(metronome, i, ctx->sample_rate);
                float phase_in = vivid::resolve_clock_phase(
                    clock_source.int_value(), vivid::audio_scalar_sample(ctx, 2, i), sample_metronome);
                advance_triggers(s, gate_val, phase_in);
                advance_adsr(s, sample_dt, attack.value, decay.value,
                             sustain.value, release.value, curve.int_value());
                out_ch[i] = s.env_value * amplitude.value + offset.value;
            }
        }
    }
};

VIVID_REGISTER(EnvelopeAudio)
VIVID_THUMBNAIL(EnvelopeAudio)
