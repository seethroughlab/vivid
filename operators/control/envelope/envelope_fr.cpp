// Frame-rate Envelope variant.
#include "envelope.h"
#include "operator_api/thumbnail.h"
#include <cstring>

struct EnvelopeFr : Envelope, vivid::FrameProcessable {
    static constexpr const char* kName = "EnvelopeFr";

    // Downgrade the shared `value` output from the base's AUDIO_BUFFER /
    // kMaxVoices declaration to a frame-rate scalar. The base's declaration
    // is correct for the audio variant (per-sample, per-voice buffer), but a
    // frame-rate operator emits one scalar per frame per lane — and the
    // lane-lifting system handles polyphony, so no multi-channel transport
    // is needed here. With SCALAR, the port is a natural match for GPU
    // params and other control inputs without requiring an explicit bridge.
    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        Envelope::collect_ports(out);
        for (auto& p : out) {
            if (p.direction == VIVID_PORT_OUTPUT && p.name
                && std::strcmp(p.name, "value") == 0) {
                p.type           = VIVID_PORT_SCALAR;
                p.transport      = VIVID_PORT_TRANSPORT_SIGNAL;
                p.channels       = 0;
                p.semantic_shape = "scalar";
                break;
            }
        }
    }

    void process_frame(const VividFrameContext* ctx) override {
        Envelope::process_frame(ctx);
    }
};

VIVID_REGISTER(EnvelopeFr)
VIVID_THUMBNAIL(EnvelopeFr)
