// Lane array sink: reads input lane data and copies it to output (passthrough).
// (Operator name kept as LaneSinkOp for test fixture compatibility.)
#include "operator_api/operator.h"

struct LaneSinkOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "LaneSinkOp";
    static constexpr bool kTimeDependent = false;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"in",  VIVID_PORT_SIGNAL, VIVID_PORT_INPUT});
        out.push_back({"out", VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        ctx->output_values[0] = ctx->input_values[0];

        // Copy input spread to output spread
        if (ctx->input_lanes && ctx->output_lanes) {
            auto& isp = ctx->input_lanes[0];
            auto& osp = ctx->output_lanes[0];
            uint32_t len = isp.length;
            if (len > osp.capacity) len = osp.capacity;
            osp.length = len;
            for (uint32_t i = 0; i < len; ++i)
                osp.data[i] = isp.data[i];
        }
    }
};

VIVID_REGISTER(LaneSinkOp)
