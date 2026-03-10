#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"

struct SyphonIn : vivid::GpuOperatorBase {
    static constexpr const char* kName = "SyphonIn";
    static constexpr bool kTimeDependent = true;

    vivid::Param<bool> active{"active", false};
    vivid::Param<int> server{"server", 0, {"Auto (First Available)"}};
    vivid::Param<vivid::TextValue> server_name{"server_name", ""};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&active);
        out.push_back(&server);
        out.push_back(&server_name);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"texture", VIVID_PORT_GPU_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void process_gpu(const VividGpuContext*) override {}
};

VIVID_REGISTER(SyphonIn)
