// Minimal export test operator that declares a custom port type.
#include "operator_api/media_stream.h"
#include "operator_api/operator.h"
#include "operator_api/port_type_registry.h"

struct ExportCustomPortOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "ExportCustomPortOp";
    static constexpr bool kTimeDependent = false;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        (void)out;
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(VIVID_CUSTOM_REF_PORT("stream", VIVID_PORT_OUTPUT, vivid::MediaStreamV1));
    }

    void process_frame(const VividFrameContext* ctx) override {
        (void)ctx;
    }
};

VIVID_REGISTER(ExportCustomPortOp)
VIVID_DESCRIBE_REF_TYPE(vivid::MediaStreamV1)
