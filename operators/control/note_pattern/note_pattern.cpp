#include "note_pattern_core.h"
#include "control/audio_scalar_utils.h"

struct NotePattern : NotePatternCore, vivid::AudioProcessable {
    static constexpr const char* kName = "NotePattern";

    void process_audio(const VividAudioContext* ctx) override {
        float local_out[3] = {};
        float beat_phase = vivid::resolve_clock_phase(
            clock_source.int_value(), vivid::audio_scalar_block_start(ctx, 0), vivid::metronome_transport(ctx));
        compute(beat_phase, ctx->param_values,
                local_out, ctx->custom_outputs, ctx->custom_output_count);
        // Broadcast scalar fallback (note/vel/gate) into output_buffers[0..2]
        // matching the SCALAR output ports declared in collect_ports().
        for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
            for (int j = 0; j < 3; ++j)
                ctx->output_buffers[j][i] = local_out[j];
        }
    }
};

VIVID_DEFINE_OP(NotePattern) {
}

VIVID_THUMBNAIL(NotePattern)
VIVID_INSPECTOR(NotePattern)
