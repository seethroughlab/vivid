#include "drum_sequencer_core.h"

struct DrumSequencerFr : DrumSequencerCore, vivid::FrameProcessable {
    static constexpr const char* kName = "drum_sequencer_fr";

    void process_frame(const VividFrameContext* ctx) override {
        compute(ctx->input_values[0], ctx->input_values[1], ctx->param_values,
                ctx->output_values, ctx->output_lanes,
                ctx->custom_outputs, ctx->custom_output_count);
    }
};

VIVID_REGISTER(DrumSequencerFr)
VIVID_THUMBNAIL(DrumSequencerFr)
VIVID_INSPECTOR(DrumSequencerFr)
