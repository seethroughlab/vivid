#include "tracker_core.h"

struct TrackerFr : TrackerCore, vivid::FrameProcessable {
    static constexpr const char* kName = "TrackerFr";

    void process_frame(const VividFrameContext* ctx) override {
        float local_in[2] = {
            vivid::resolve_clock_phase(clock_source.int_value(), ctx->input_values[0], vivid::metronome_transport(ctx)),
            ctx->input_values[1],
        };
        compute(local_in, ctx->param_values, ctx->output_lanes,
                ctx->output_values, ctx->custom_outputs, ctx->custom_output_count);
    }
};

VIVID_REGISTER(TrackerFr)
VIVID_THUMBNAIL(TrackerFr)
VIVID_INSPECTOR(TrackerFr)
