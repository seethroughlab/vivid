// Test operator: copies VividFrameContext lane metadata to scalar outputs.
// Used by test_lane_metadata to verify the frame executor populates
// lane_count, lane_index, and lane_id correctly.
#include "operator_api/operator.h"

struct LaneMetadataOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "LaneMetadataOp";
    static constexpr bool kTimeDependent = false;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"in",          VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"lane_count",  VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"lane_index",  VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"lane_id",     VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    void process_frame(const VividFrameContext* ctx) override {
        ctx->output_values[0] = static_cast<float>(ctx->lane_count);
        ctx->output_values[1] = static_cast<float>(ctx->lane_index);
        ctx->output_values[2] = static_cast<float>(ctx->lane_id);
    }
};

