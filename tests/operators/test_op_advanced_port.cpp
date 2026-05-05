#include "operator_api/operator.h"

/**
 * @brief Fixture operator that declares one default + one advanced output.
 *
 * Used by tests/ui/test_advanced_port_filter.cpp to verify the descriptor
 * round-trips display_hint, the graph compiler populates
 * advanced_output_port_indices, and the inspector hides the advanced port
 * on the node body until a connection lands on it.
 */
struct TestOpAdvancedPort : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "TestOpAdvancedPort";
    static constexpr bool kTimeDependent = false;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        (void)out;
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"primary",  VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"advanced", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        vivid::advanced_breakout(out.back());
    }

    void process_frame(const VividFrameContext* ctx) override {
        ctx->output_values[0] = 1.0f;
        ctx->output_values[1] = 2.0f;
    }
};

