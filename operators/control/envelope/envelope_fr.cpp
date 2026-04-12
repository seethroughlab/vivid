// Frame-rate Envelope variant.
#include "envelope.h"
#include "operator_api/thumbnail.h"

struct EnvelopeFr : Envelope, vivid::FrameProcessable {
    static constexpr const char* kName = "EnvelopeFr";

    void process_frame(const VividFrameContext* ctx) override {
        Envelope::process_frame(ctx);
    }
};

VIVID_REGISTER(EnvelopeFr)
VIVID_THUMBNAIL(EnvelopeFr)
