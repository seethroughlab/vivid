// Control-domain passthrough with spread support: output = input * gain
#include "operator_api/operator.h"

struct ControlPassOp : vivid::ControlOperatorBase {
    static constexpr const char* kName   = "ControlPassOp";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> gain{"gain", 1.0f, 0.0f, 100.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&gain);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"in",  VIVID_PORT_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"out", VIVID_PORT_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        float g = ctx->param_values[0];
        float in = ctx->input_values[0];
        ctx->output_values[0] = in * g;

        // Spread propagation: multiply each spread element by gain
        if (ctx->input_spreads && ctx->output_spreads) {
            const auto& isp = ctx->input_spreads[0];
            auto& osp = ctx->output_spreads[0];
            if (isp.length > 0 && osp.capacity >= isp.length) {
                osp.length = isp.length;
                for (uint32_t i = 0; i < isp.length; ++i) {
                    osp.data[i] = isp.data[i] * g;
                }
            }
        }
    }
};

VIVID_REGISTER(ControlPassOp)
