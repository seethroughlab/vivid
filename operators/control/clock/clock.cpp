#include "operator_api/operator.h"
#include <cmath>

struct Clock : vivid::OperatorBase {
    static constexpr const char* kName   = "Clock";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_CONTROL;
    static constexpr bool kTimeDependent = true;

    vivid::Param<float> bpm{"bpm", 120.0f, 1.0f, 300.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&bpm);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"beat_phase", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        double beats_per_sec = static_cast<double>(bpm.value) / 60.0;
        double beat_phase = std::fmod(ctx->time * beats_per_sec, 1.0);
        ctx->output_values[0] = static_cast<float>(beat_phase);
    }
};

VIVID_REGISTER(Clock)
