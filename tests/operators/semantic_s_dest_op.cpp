#include "operator_api/operator.h"

struct SecDestOp : vivid::OperatorBase {
    static constexpr const char* kName   = "SecDestOp";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_CONTROL;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> sec{"sec", 0.5f, 0.0f, 2.0f};

    SecDestOp() {
        vivid::semantic_tag(sec, "time_seconds");
        vivid::semantic_shape(sec, "scalar");
        vivid::semantic_unit(sec, "s");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&sec);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"out", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(VividProcessContext* ctx) override {
        ctx->output_values[0] = ctx->param_values[0];
    }
};

VIVID_REGISTER(SecDestOp)
