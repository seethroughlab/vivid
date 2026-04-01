#include "arpeggiator_core.h"

struct ArpeggiatorFr : ArpeggiatorCore, vivid::FrameProcessable {
    static constexpr const char* kName = "arpeggiator_fr";

    void process_frame(const VividFrameContext* ctx) override {
        compute(ctx->input_values[0], ctx->param_values, ctx->input_lanes,
                ctx->output_values, ctx->output_lanes,
                ctx->custom_outputs, ctx->custom_output_count);
    }
};

VIVID_REGISTER(ArpeggiatorFr)
VIVID_THUMBNAIL(ArpeggiatorFr)
VIVID_INSPECTOR(ArpeggiatorFr)
