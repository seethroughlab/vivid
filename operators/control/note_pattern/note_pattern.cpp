#include "note_pattern_core.h"
#include "control/audio_scalar_utils.h"

struct NotePattern : NotePatternCore, vivid::AudioProcessable {
    static constexpr const char* kName = "NotePattern";

    void process_audio(const VividAudioContext* ctx) override {
        float local_out[3] = {};
        float beat_phase = vivid::resolve_clock_phase(
            clock_source.int_value(), vivid::audio_scalar_block_start(ctx, 0), vivid::metronome_transport(ctx));
        compute(beat_phase, ctx->param_values, ctx->output_lanes,
                local_out, ctx->custom_outputs, ctx->custom_output_count);
    }
};

VIVID_REGISTER(NotePattern)
VIVID_THUMBNAIL(NotePattern)
VIVID_INSPECTOR(NotePattern)
