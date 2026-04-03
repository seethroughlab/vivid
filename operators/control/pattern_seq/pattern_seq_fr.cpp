#include "pattern_seq_core.h"

struct PatternSeq_FR : PatternSeqCore, vivid::FrameProcessable {
    static constexpr const char* kName = "PatternSeqFr";

    void process_frame(const VividFrameContext* ctx) override {
        compute(ctx->input_values[0], ctx->param_values, ctx->output_values,
                ctx->output_lanes, ctx->custom_outputs, ctx->custom_output_count);
    }
};

VIVID_REGISTER(PatternSeq_FR)
