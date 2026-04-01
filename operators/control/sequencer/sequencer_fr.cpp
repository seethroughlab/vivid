#include "sequencer_core.h"

struct SequencerFr : SequencerCore, vivid::FrameProcessable {
    static constexpr const char* kName = "sequencer_fr";

    void process_frame(const VividFrameContext* ctx) override {
        compute(ctx->input_values[0], ctx->input_values[1],
                ctx->input_lanes, ctx->output_values,
                ctx->custom_outputs, ctx->custom_output_count);
    }
};

VIVID_REGISTER(SequencerFr)
