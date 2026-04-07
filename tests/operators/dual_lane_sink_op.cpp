// Dual lane-array sink: two lane_array inputs, copies each to a corresponding
// output lane array.  Used to verify scalar-to-lane broadcasting.
#include "operator_api/operator.h"

struct DualLaneSinkOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "DualLaneSinkOp";
    static constexpr bool kTimeDependent = false;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"in_a", VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT});
        out.push_back({"in_b", VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT});
        out.push_back({"out_a", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"out_b", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        ctx->output_values[0] = ctx->input_values[0];
        ctx->output_values[1] = ctx->input_values[1];

        if (!ctx->input_lanes || !ctx->output_lanes) return;

        // Copy input lane arrays to output lane arrays
        for (int port = 0; port < 2; ++port) {
            const auto& isp = ctx->input_lanes[port];
            auto& osp = ctx->output_lanes[port];
            uint32_t len = isp.length;
            float* buf = osp.resize(osp.handle, len);
            if (buf) {
                for (uint32_t i = 0; i < len; ++i)
                    buf[i] = isp.data[i];
                osp.commit(osp.handle, len);
            }
        }
    }
};

VIVID_REGISTER(DualLaneSinkOp)
