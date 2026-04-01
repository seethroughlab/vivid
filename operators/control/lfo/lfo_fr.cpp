// Frame-rate LFO variant. Reuses the full LFO implementation from lfo.h
// but only exposes FrameProcessable. The process_frame() body is copied
// from LFO::process_frame() to preserve lane-state behavior exactly.
#include "lfo.h"

struct LfoFr : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "lfo_fr";
    static constexpr bool kTimeDependent = true;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_POINTWISE;
    static constexpr bool kStrategyIndependent = true;

    // Reuse all member state from LFO.
    LFO impl_;

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

VIVID_REGISTER(LfoFr)
