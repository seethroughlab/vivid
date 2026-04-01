#include "chord_progression_core.h"

struct ChordProgressionFr : ChordProgressionCore, vivid::FrameProcessable {
    static constexpr const char* kName = "chord_progression_fr";

    void process_frame(const VividFrameContext* ctx) override {
        compute(ctx->input_values[0], ctx->param_values, ctx->output_lanes,
                ctx->output_values, ctx->custom_outputs, ctx->custom_output_count);
    }
};

VIVID_REGISTER(ChordProgressionFr)
VIVID_THUMBNAIL(ChordProgressionFr)
