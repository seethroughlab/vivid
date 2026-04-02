#include "clock_core.h"
#include "operator_api/thumbnail.h"

struct ClockFr : ClockCore, vivid::FrameProcessable {
    static constexpr const char* kName = "ClockFr";

    void process_frame(const VividFrameContext* ctx) override {
        float out4[4];
        advance(ctx->delta_time, out4);
        for (int i = 0; i < 4; ++i)
            ctx->output_values[i] = out4[i];
    }
};

VIVID_REGISTER(ClockFr)
VIVID_THUMBNAIL(ClockFr)
