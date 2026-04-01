#include "tracker_core.h"

struct TrackerFr : TrackerCore, vivid::FrameProcessable {
    static constexpr const char* kName = "tracker_fr";

    void process_frame(const VividFrameContext* ctx) override {
        compute(ctx->input_values, ctx->param_values, ctx->output_lanes,
                ctx->output_values, ctx->custom_outputs, ctx->custom_output_count);
    }
};

VIVID_REGISTER(TrackerFr)
VIVID_THUMBNAIL(TrackerFr)
VIVID_INSPECTOR(TrackerFr)
