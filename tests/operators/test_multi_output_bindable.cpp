// Bindable control operator with two outputs ("value" and "phase").
// Used to test output-aware role binding selection.
#include "operator_api/operator.h"

struct TestMultiOutputBindable : vivid::ControlOperatorBase {
    static constexpr const char* kName   = "TestMultiOutputBindable";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> rate{"rate", 1.0f, 0.0f, 20.0f};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&rate);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        VividPortDescriptor value_port{};
        value_port.name = "value";
        value_port.type = VIVID_PORT_SIGNAL;
        value_port.direction = VIVID_PORT_OUTPUT;
        value_port.semantic_tag = "envelope";
        out.push_back(value_port);

        VividPortDescriptor phase_port{};
        phase_port.name = "phase";
        phase_port.type = VIVID_PORT_SIGNAL;
        phase_port.direction = VIVID_PORT_OUTPUT;
        phase_port.semantic_tag = "phase";
        out.push_back(phase_port);
    }

    void process(const VividProcessContext* ctx) override {
        ctx->output_values[0] = ctx->param_values[0] * 0.5f;  // value
        ctx->output_values[1] = ctx->param_values[0] * 0.1f;  // phase
    }
};

VIVID_REGISTER(TestMultiOutputBindable)
VIVID_BINDABLE(TestMultiOutputBindable)
