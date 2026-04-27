#include "chord_progression_core.h"
#include "control/audio_scalar_utils.h"

struct ChordProgression : ChordProgressionCore, vivid::AudioProcessable {
    static constexpr const char* kName = "ChordProgression";

    void process_audio(const VividAudioContext* ctx) override {
        float local_out[3] = {};
        float beat_phase = vivid::resolve_clock_phase(
            clock_source.int_value(), vivid::audio_scalar_block_start(ctx, 0), vivid::metronome_transport(ctx));
        compute(beat_phase, ctx->param_values,
                local_out, ctx->custom_outputs, ctx->custom_output_count);
        // SCALAR outputs (note/vel/gate) are now ports [0..2] — the legacy
        // LANE_ARRAY notes/velocities/gates outputs were removed in PR3.
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            for (int j = 0; j < 3; ++j)
                ctx->output_buffers[j][i] = local_out[j];
        }
    }
};

VIVID_REGISTER(ChordProgression)
VIVID_THUMBNAIL(ChordProgression)
VIVID_INSPECTOR(ChordProgression)
