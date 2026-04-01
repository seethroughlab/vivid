#include "note_pattern_core.h"

struct NotePatternAu : NotePatternCore, vivid::AudioProcessable {
    static constexpr const char* kName = "note_pattern_au";

    void process_audio(const VividAudioContext* ctx) override {
        compute(ctx->input_float_values[0], ctx->param_values, ctx->output_lanes,
                ctx->output_float_values, ctx->custom_outputs, ctx->custom_output_count);
    }
};

VIVID_REGISTER(NotePatternAu)
VIVID_THUMBNAIL(NotePatternAu)
VIVID_INSPECTOR(NotePatternAu)
