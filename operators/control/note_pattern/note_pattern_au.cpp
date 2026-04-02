#include "note_pattern_core.h"
#include "control/audio_scalar_utils.h"

struct NotePatternAu : NotePatternCore, vivid::AudioProcessable {
    static constexpr const char* kName = "note_pattern_au";

    void process_audio(const VividAudioContext* ctx) override {
        float local_out[3] = {};
        float beat_phase = vivid::audio_scalar_block_start(ctx, 0);
        compute(beat_phase, ctx->param_values, ctx->output_lanes,
                local_out, ctx->custom_outputs, ctx->custom_output_count);
    }
};

VIVID_REGISTER(NotePatternAu)
VIVID_THUMBNAIL(NotePatternAu)
VIVID_INSPECTOR(NotePatternAu)
