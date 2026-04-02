// Frame-rate LFO variant.
#include "lfo.h"
#include "operator_api/thumbnail.h"

struct LfoFr : LFO, vivid::FrameProcessable {
    static constexpr const char* kName = "LfoFr";

    void process_frame(const VividFrameContext* ctx) override {
        LFO::process_frame(ctx);
    }
};

VIVID_REGISTER(LfoFr)
VIVID_THUMBNAIL(LfoFr)
