// Frame-rate MSEG variant.
#include "mseg.h"
#include "operator_api/thumbnail.h"

struct MSEG_FR : MSEG, vivid::FrameProcessable {
    static constexpr const char* kName = "MsegFr";

    void process_frame(const VividFrameContext* ctx) override {
        MSEG::process_frame(ctx);
    }
};

VIVID_REGISTER(MSEG_FR)
VIVID_THUMBNAIL(MSEG_FR)
VIVID_INSPECTOR(MSEG_FR)
