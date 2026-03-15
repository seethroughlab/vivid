// Test operator that declares a conflicting custom port type registration.
#include "operator_api/media_stream.h"
#include "operator_api/operator.h"
#include "operator_api/port_type_registry.h"

struct BadMediaStreamToken {
    uint64_t handle_id = 0;
};

VIVID_DECLARE_CUSTOM_REF_TYPE(BadMediaStreamToken,
                              "seethroughlab.vivid.media_stream_v1",
                              "BadMediaStreamToken",
                              false);

struct BadCustomTypeOp : vivid::ControlOperatorBase {
    static constexpr const char* kName = "BadCustomTypeOp";
    static constexpr bool kTimeDependent = false;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        (void)out;
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(VIVID_CUSTOM_REF_PORT("stream", VIVID_PORT_OUTPUT, BadMediaStreamToken));
    }

    void process(const VividProcessContext* ctx) override {
        (void)ctx;
    }
};

VIVID_REGISTER(BadCustomTypeOp)
VIVID_DESCRIBE_REF_TYPE(BadMediaStreamToken)
