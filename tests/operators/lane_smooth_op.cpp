// Test operator: kernel behavior proof-of-concept.
// Reads all lanes from input spread and writes a 3-element moving average
// across lanes to the output spread. Demonstrates cross-lane access.
#include "operator_api/operator.h"
#include <algorithm>

struct LaneSmoothOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "LaneSmoothOp";
    static constexpr bool kTimeDependent = false;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_KERNEL;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"in",  VIVID_PORT_SIGNAL, VIVID_PORT_INPUT});
        out.push_back({"out", VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        // Pass through scalar value
        ctx->output_values[0] = ctx->input_values[0];

        // Kernel behavior: read full input spread, smooth across lanes
        if (ctx->input_spreads && ctx->output_spreads) {
            auto& isp = ctx->input_spreads[0];
            auto& osp = ctx->output_spreads[0];
            uint32_t len = isp.length;
            if (len > osp.capacity) len = osp.capacity;
            osp.length = len;

            for (uint32_t i = 0; i < len; ++i) {
                // 3-element moving average: average with neighbors
                float prev = (i > 0) ? isp.data[i - 1] : isp.data[i];
                float curr = isp.data[i];
                float next = (i + 1 < len) ? isp.data[i + 1] : isp.data[i];
                osp.data[i] = (prev + curr + next) / 3.0f;
            }
        }
    }
};

VIVID_REGISTER(LaneSmoothOp)
