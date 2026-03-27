// Minimal export test operator that declares a custom port type.
#include "operator_api/operator.h"
#include "operator_api/port_type_registry.h"

namespace test {
struct TestCustomRef {
    uint64_t handle_id = 0;
    uint64_t generation = 0;
};
} // namespace test

VIVID_DECLARE_CUSTOM_REF_TYPE(test::TestCustomRef,
                              "seethroughlab.vivid.test_custom_ref",
                              "TestCustomRef",
                              false);

struct ExportCustomPortOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "ExportCustomPortOp";
    static constexpr bool kTimeDependent = false;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        (void)out;
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(VIVID_CUSTOM_REF_PORT("stream", VIVID_PORT_OUTPUT, test::TestCustomRef));
    }

    void process_frame(const VividFrameContext* ctx) override {
        (void)ctx;
    }
};

VIVID_REGISTER(ExportCustomPortOp)
VIVID_DESCRIBE_REF_TYPE(test::TestCustomRef)
