#include "note_pattern_core.h"

struct NotePatternFr : NotePatternCore, vivid::FrameProcessable {
    static constexpr const char* kName = "NotePatternFr";

    void process_frame(const VividFrameContext* ctx) override {
        float beat_phase = vivid::resolve_clock_phase(
            clock_source.int_value(), ctx->input_values[0], vivid::metronome_transport(ctx));
        compute(beat_phase, ctx->param_values, ctx->output_lanes,
                ctx->output_values, ctx->custom_outputs, ctx->custom_output_count);
    }
};

VIVID_REGISTER(NotePatternFr)
VIVID_THUMBNAIL(NotePatternFr)
VIVID_INSPECTOR(NotePatternFr)
