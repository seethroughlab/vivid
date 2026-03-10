// Spread sink: reads input spread and copies it to output spread (passthrough).
// Used by test_spread_broadcast to observe the resulting spread after broadcasting.
#include "operator_api/operator.h"

struct SpreadSinkOp : vivid::ControlOperatorBase {
    static constexpr const char* kName   = "SpreadSinkOp";
    static constexpr bool kTimeDependent = false;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"in",  VIVID_PORT_FLOAT, VIVID_PORT_INPUT});
        out.push_back({"out", VIVID_PORT_FLOAT, VIVID_PORT_OUTPUT});
    }

    void process(const VividProcessContext* ctx) override {
        ctx->output_values[0] = ctx->input_values[0];

        // Copy input spread to output spread
        if (ctx->input_spreads && ctx->output_spreads) {
            auto& isp = ctx->input_spreads[0];
            auto& osp = ctx->output_spreads[0];
            uint32_t len = isp.length;
            if (len > osp.capacity) len = osp.capacity;
            osp.length = len;
            for (uint32_t i = 0; i < len; ++i)
                osp.data[i] = isp.data[i];
        }
    }
};

VIVID_REGISTER(SpreadSinkOp)
