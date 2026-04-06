#include "euclidean_core.h"
#include "operator_api/thumbnail.h"

struct EuclideanFr : EuclideanCore, vivid::FrameProcessable {
    static constexpr const char* kName = "EuclideanFr";

    void process_frame(const VividFrameContext* ctx) override {
        float beat_phase = vivid::resolve_clock_phase(
            clock_source.int_value(), ctx->input_values[0], vivid::metronome_transport(ctx));
        compute(beat_phase, ctx->param_values,
                ctx->output_lanes, ctx->output_values);
    }
};

VIVID_REGISTER(EuclideanFr)
VIVID_THUMBNAIL(EuclideanFr)
