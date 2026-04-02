// Frame-rate Gate variant.
#include "gate.h"
#include "operator_api/thumbnail.h"

struct GateFr : Gate, vivid::FrameProcessable {
    static constexpr const char* kName = "GateFr";

    void process_frame(const VividFrameContext* ctx) override {
        Gate::process_frame_impl(ctx);
    }
};

VIVID_REGISTER(GateFr)
VIVID_THUMBNAIL(GateFr)
