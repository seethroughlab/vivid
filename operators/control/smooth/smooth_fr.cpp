#include "smooth.h"
#include "operator_api/thumbnail.h"

struct SmoothFr : Smooth {
    static constexpr const char* kName = "SmoothFr";

    void process_frame(const VividFrameContext* ctx) override {
        Smooth::process_frame(ctx);
    }
};

VIVID_REGISTER(SmoothFr)
VIVID_THUMBNAIL(SmoothFr)
