#include "operator_api/operator.h"

/**
 * @brief Fixture operator docs for MCP/control-server tests.
 *
 * Detailed body for the TestOp fixture.
 *
 * @tip Use this fixture to verify MCP doc merging.
 * @param scale Scale multiplier for the output.
 * @output out Scaled scalar output.
 * @family test-fixture
 */
struct TestOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "TestOp";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> scale{"scale", 1.0f, 0.0f, 100.0f};

    TestOp() {
        vivid::semantic_tag(scale, "frequency_hz");
        vivid::semantic_shape(scale, "scalar");
        vivid::semantic_unit(scale, "Hz");
        vivid::semantic_intent(scale, "test_scale");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&scale);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        vivid::semantic_tag(out.back(), "test_scalar_out");
        vivid::semantic_shape(out.back(), "scalar");
        vivid::semantic_intent(out.back(), "test_output");
        vivid::description(out.back(), "Scaled test output used by loader and control-server tests.");
    }

    void process_frame(const VividFrameContext* ctx) override {
        ctx->output_values[0] = ctx->param_values[0] * 2.0f;
    }
};

VIVID_REGISTER(TestOp)
