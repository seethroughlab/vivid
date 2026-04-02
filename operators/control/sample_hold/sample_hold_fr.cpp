// Frame-rate SampleHold variant.
#include "sample_hold.h"
#include "operator_api/thumbnail.h"

struct SampleHoldFr : SampleHold, vivid::FrameProcessable {
    static constexpr const char* kName = "SampleHoldFr";

    void process_frame(const VividFrameContext* ctx) override {
        SampleHold::process_frame_impl(ctx);
    }
};

VIVID_REGISTER(SampleHoldFr)
VIVID_THUMBNAIL(SampleHoldFr)
