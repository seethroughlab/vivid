#include "sequencer_core.h"

struct SequencerFr : SequencerCore, vivid::FrameProcessable {
    static constexpr const char* kName = "SequencerFr";

    void process_frame(const VividFrameContext* ctx) override {
        compute(ctx->input_values, ctx->delta_time,
                ctx->input_lanes, ctx->output_values,
                ctx->custom_outputs, ctx->custom_output_count,
                vivid::metronome_transport(ctx));
    }
};

VIVID_REGISTER(SequencerFr)
