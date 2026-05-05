#include "drum_sequencer_core.h"
#include "control/audio_scalar_utils.h"

struct DrumSequencer : DrumSequencerCore, vivid::AudioProcessable {
    static constexpr const char* kName = "DrumSequencer";

    void process_audio(const VividAudioContext* ctx) override {
        // Two scalar outputs: [0] step, [1] current_pattern (0..3).
        float local_out[2] = {};
        const auto m = vivid::metronome_transport(ctx);
        float beat_phase = vivid::resolve_bar_phase(
            clock_source.int_value(), vivid::audio_scalar_block_start(ctx, 0), m);
        float reset = vivid::audio_scalar_block_start(ctx, 1);
        compute(beat_phase, reset, m.beats_elapsed, m.beats_per_bar,
                ctx->param_values,
                local_out, ctx->output_lanes,
                ctx->custom_outputs, ctx->custom_output_count);
        // Runtime sizes output_buffers[] from collect_ports(), so both
        // scalar outputs are guaranteed allocated.
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            ctx->output_buffers[0][i] = local_out[0];
            ctx->output_buffers[1][i] = local_out[1];
        }
    }
};

VIVID_DEFINE_OP(DrumSequencer) {
}

VIVID_THUMBNAIL(DrumSequencer)
VIVID_EDITOR(DrumSequencer)
