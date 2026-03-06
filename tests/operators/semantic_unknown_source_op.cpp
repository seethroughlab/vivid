#include "operator_api/operator.h"

struct UnknownTagSourceOp : vivid::OperatorBase {
    static constexpr const char* kName   = "UnknownTagSourceOp";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_CONTROL;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> value{"value", 0.5f, 0.0f, 1.0f};

    UnknownTagSourceOp() {
        vivid::semantic_tag(value, "x_test_unknown_scalar");
        vivid::semantic_shape(value, "scalar");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&value);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"out", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        ctx->output_values[0] = ctx->param_values[0];
    }
};

VIVID_REGISTER(UnknownTagSourceOp)
