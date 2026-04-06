// Audio-rate LFO variant.
#include "lfo.h"
#include "control/audio_scalar_utils.h"
#include "operator_api/thumbnail.h"

struct LfoAu : LFO, vivid::AudioProcessable {
    static constexpr const char* kName = "LfoAu";

    void process_audio(const VividAudioContext* ctx) override {
        LaneState& s = ctx->lane_state_fn
            ? *vivid_lane_state(ctx, ctx->lane_id, LaneState)
            : scalar_state_;

        const double sample_dt = 1.0 / static_cast<double>(ctx->sample_rate);
        const vivid::MetronomeTransport base_metronome = vivid::metronome_transport(ctx);
        const double beats_per_sample = (base_metronome.bpm > 0.0f)
            ? (static_cast<double>(base_metronome.bpm) / 60.0) / static_cast<double>(ctx->sample_rate)
            : 0.0;

        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            vivid::MetronomeTransport sample_metronome = base_metronome;
            if (sample_metronome.enabled) {
                const double beat_offset = static_cast<double>(i) * beats_per_sample;
                sample_metronome.beats_elapsed += beat_offset;
                sample_metronome.beat_phase = static_cast<float>(
                    std::fmod(static_cast<double>(sample_metronome.beat_phase) + beat_offset, 1.0));
            }
            ctx->output_buffers[0][i] = compute_one_sample(
                s, frequency.value, amplitude.value, offset.value,
                waveform.int_value(), rate_mode.int_value(), sync_division.int_value(),
                polarity.int_value(), distribution.int_value(),
                seed.int_value(), static_cast<float>(phase_offset.value),
                fade_in.value,
                vivid::audio_scalar_sample(ctx, 0, i),
                vivid::audio_scalar_sample(ctx, 1, i),
                sample_dt, slew.value, sample_metronome);
        }
    }
};

VIVID_REGISTER(LfoAu)
VIVID_THUMBNAIL(LfoAu)
