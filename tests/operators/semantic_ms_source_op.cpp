#include "operator_api/operator.h"

struct MsSourceOp : vivid::OperatorBase {
    static constexpr const char* kName   = "MsSourceOp";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_CONTROL;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> ms{"ms", 500.0f, 0.0f, 2000.0f};

    MsSourceOp() {
        vivid::semantic_tag(ms, "time_milliseconds");
        vivid::semantic_shape(ms, "scalar");
        vivid::semantic_unit(ms, "ms");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&ms);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"ms", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        ctx->output_values[0] = ctx->param_values[0];
    }
};

VIVID_REGISTER(MsSourceOp)
