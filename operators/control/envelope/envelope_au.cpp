// Audio-rate Envelope variant. Reuses the full Envelope implementation
// from envelope.h but only exposes AudioProcessable.
#include "envelope.h"

struct EnvelopeAu : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName = "envelope_au";
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

    void process_audio(const VividAudioContext* ctx) override {
        impl_.process_audio(ctx);
    }
};

VIVID_REGISTER(EnvelopeAu)
