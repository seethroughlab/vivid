#include "note_pattern_core.h"

struct NotePatternFr : NotePatternCore, vivid::FrameProcessable {
    static constexpr const char* kName = "note_pattern_fr";

    void process_frame(const VividFrameContext* ctx) override {
        compute(ctx->input_values[0], ctx->param_values, ctx->output_lanes,
                ctx->output_values, ctx->custom_outputs, ctx->custom_output_count);
    }
};

VIVID_REGISTER(NotePatternFr)
VIVID_THUMBNAIL(NotePatternFr)
VIVID_INSPECTOR(NotePatternFr)
