// Lane-array source used by the audio-frame bridge snapshot tests.
// Generates lane data [base*1, base*2, ..., base*count].
#include "operator_api/operator.h"

struct LaneSourceOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "LaneSourceOp";
    static constexpr bool kTimeDependent = false;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_STRUCTURAL;

    vivid::Param<float> base{"base", 1.0f, 0.0f, 100.0f};
    vivid::Param<int>   count{"count", 4, 1, 64};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&base);
        out.push_back(&count);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        float b = ctx->param_values[0];
        int   n = static_cast<int>(ctx->param_values[1]);

        ctx->output_values[0] = b;

        // Write lane array: [base*1, base*2, ..., base*count]
        if (ctx->output_lanes) {
            auto& osp = ctx->output_lanes[0];
            uint32_t len = static_cast<uint32_t>(n);
            if (osp.capacity >= len) {
                osp.length = len;
                for (uint32_t i = 0; i < len; ++i) {
                    osp.data[i] = b * static_cast<float>(i + 1);
                }
            }
        }
    }
};

VIVID_REGISTER(LaneSourceOp)
