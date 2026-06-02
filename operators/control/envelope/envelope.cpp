// Audio-rate Envelope variant with polyphonic (lane-array) gate support.
#include "envelope.h"
#include "control/audio_scalar_utils.h"
#include "operator_api/thumbnail.h"

#include <algorithm>
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
        uint32_t current_voice_count = (gate_lane && gate_lane->data) ? gate_lane->length : 0;
        if (current_voice_count > kMaxVoices) current_voice_count = kMaxVoices;

        bool current_present[kMaxVoices] = {};
        float current_gate_values[kMaxVoices] = {};
        for (uint32_t vi = 0; vi < current_voice_count; ++vi) {
            uint32_t lid = (lane_id_lane && lane_id_lane->data && vi < lane_id_lane->length)
                ? static_cast<uint32_t>(lane_id_lane->data[vi]) : vi;
            int tracked = ensure_tracked_lane(lid);
            if (tracked < 0) continue;
            current_present[tracked] = true;
            current_gate_values[tracked] = gate_lane->data[vi];
        }

        // Fallback: scalar (non-polyphonic) mode
        bool any_tracked = false;
        for (uint32_t i = 0; i < kMaxVoices; ++i) {
            if (tracked_lanes_[i].occupied) {
                any_tracked = true;
                break;
            }
        }
        if (!any_tracked) {
            LaneState& s = ctx->lane_state_fn
                ? *vivid_lane_state(ctx, ctx->lane_id, LaneState)
                : scalar_state_;
            for (uint32_t i = 0; i < frames; ++i) {
                float gate_in = vivid::audio_scalar_sample(ctx, 0, i);
                vivid::MetronomeTransport sample_metronome =
                    vivid::metronome_transport_sample(metronome, i, ctx->sample_rate);
                float phase_in = vivid::resolve_clock_phase(
                    clock_mode.int_value(), vivid::audio_scalar_sample(ctx, 2, i), sample_metronome);
                advance_triggers(s, gate_in, phase_in);
                advance_adsr(s, sample_dt, attack.value, decay.value,
                             sustain.value, release.value, curve.int_value());
                ctx->output_buffers[0][i] = s.env_value * amplitude.value + offset.value;
            }
            return;
        }

        // Polyphonic: one output channel per tracked voice ID, preserving
        // release tails even when the current gate/lane_id inputs shrink.
        std::memset(ctx->output_buffers[0], 0, kMaxVoices * frames * sizeof(float));

        int emit_indices[kMaxVoices];
        int emit_count = 0;
        for (uint32_t i = 0; i < kMaxVoices; ++i) {
            if (tracked_lanes_[i].occupied) emit_indices[emit_count++] = static_cast<int>(i);
        }
        std::sort(emit_indices, emit_indices + emit_count,
                  [this](int a, int b) {
                      return tracked_lanes_[a].lane_id < tracked_lanes_[b].lane_id;
                  });

        for (int out_idx = 0; out_idx < emit_count; ++out_idx) {
            const int tracked_idx = emit_indices[out_idx];
            const uint32_t lid = tracked_lanes_[tracked_idx].lane_id;
            const float gate_val = current_present[tracked_idx] ? current_gate_values[tracked_idx] : 0.0f;

            LaneState& s = ctx->lane_state_fn
                ? *vivid_lane_state(ctx, lid, LaneState)
                : tracked_lanes_[tracked_idx].fallback_state;

            float* out_ch = ctx->output_buffers[0] + static_cast<uint32_t>(out_idx) * frames;

            for (uint32_t i = 0; i < frames; ++i) {
                vivid::MetronomeTransport sample_metronome =
                    vivid::metronome_transport_sample(metronome, i, ctx->sample_rate);
                float phase_in = vivid::resolve_clock_phase(
                    clock_mode.int_value(), vivid::audio_scalar_sample(ctx, 2, i), sample_metronome);
                advance_triggers(s, gate_val, phase_in);
                advance_adsr(s, sample_dt, attack.value, decay.value,
                             sustain.value, release.value, curve.int_value());
                out_ch[i] = s.env_value * amplitude.value + offset.value;
            }

            if (!current_present[tracked_idx] && s.stage == IDLE && !s.prev_gate) {
                clear_tracked_lane(tracked_idx);
            }
        }
    }
};

VIVID_DEFINE_OP(EnvelopeAudio) {
}

VIVID_THUMBNAIL(EnvelopeAudio)
