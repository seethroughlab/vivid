#include "note_pattern_core.h"

struct NotePatternAu : NotePatternCore, vivid::AudioProcessable {
    static constexpr const char* kName = "note_pattern_au";

    void process_audio(const VividAudioContext* ctx) override {
        float local_out[3] = {};
        compute(0.0f, ctx->param_values, ctx->output_lanes,
                local_out, ctx->custom_outputs, ctx->custom_output_count);
    }
};

VIVID_REGISTER(NotePatternAu)
VIVID_THUMBNAIL(NotePatternAu)
VIVID_INSPECTOR(NotePatternAu)
