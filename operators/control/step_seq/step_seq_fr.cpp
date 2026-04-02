// Frame-rate StepSeq variant.
#include "step_seq.h"

struct StepSeq_FR : StepSeq, vivid::FrameProcessable {
    static constexpr const char* kName = "StepSeqFr";

    void process_frame(const VividFrameContext* ctx) override {
        compute(ctx->input_values, ctx->delta_time, ctx->output_values);
    }
};

VIVID_REGISTER(StepSeq_FR)
VIVID_INSPECTOR(StepSeq_FR)
