#include "euclidean_core.h"
#include "operator_api/thumbnail.h"

struct EuclideanFr : EuclideanCore, vivid::FrameProcessable {
    static constexpr const char* kName = "EuclideanFr";

    void process_frame(const VividFrameContext* ctx) override {
        compute(ctx->input_values[0], ctx->param_values,
                ctx->output_lanes, ctx->output_values);
    }
};

VIVID_REGISTER(EuclideanFr)
VIVID_THUMBNAIL(EuclideanFr)
