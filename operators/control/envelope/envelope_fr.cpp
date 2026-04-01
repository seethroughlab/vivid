// Frame-rate Envelope variant. Reuses the full Envelope implementation
// from envelope.h but only exposes FrameProcessable.
#include "envelope.h"

struct EnvelopeFr : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "envelope_fr";
    static constexpr bool kTimeDependent = true;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_POINTWISE;
    static constexpr bool kStrategyIndependent = true;

    Envelope impl_;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        impl_.collect_params(out);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        impl_.collect_ports(out);
    }

    void process_frame(const VividFrameContext* ctx) override {
        impl_.process_frame(ctx);
    }
};

VIVID_REGISTER(EnvelopeFr)
