// Test operator that declares a conflicting custom port type registration.
#include "operator_api/operator.h"
#include "operator_api/port_type_registry.h"

// Declares a custom type with the same stable_type_id as ExportCustomPortOp's
// TestCustomRef but with a different payload size, creating a conflict that
// the registry should reject.
struct BadMediaStreamToken {
    uint64_t handle_id = 0;
    uint64_t extra_field = 0;
    uint64_t another_field = 0;  // different payload size than TestCustomRef
};

VIVID_DECLARE_CUSTOM_REF_TYPE(BadMediaStreamToken,
                              "seethroughlab.vivid.test_custom_ref",
                              "BadMediaStreamToken",
                              false);

struct BadCustomTypeOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "BadCustomTypeOp";
    static constexpr bool kTimeDependent = false;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        (void)out;
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(VIVID_CUSTOM_REF_PORT("stream", VIVID_PORT_OUTPUT, BadMediaStreamToken));
    }

    void process_frame(const VividFrameContext* ctx) override {
        (void)ctx;
    }
};

VIVID_REGISTER(BadCustomTypeOp)
VIVID_DESCRIBE_REF_TYPE(BadMediaStreamToken)
