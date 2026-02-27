#include "operator_api/wgsl_filter.h"

struct Switch : vivid::WgslFilterBase {
    static constexpr const char* kName = "Switch";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = false;

    vivid::Param<int>   index {"index", 0, 0, 3};
    vivid::Param<float> blend {"blend", 0.0f, 0.0f, 1.0f};

    Switch() : WgslFilterBase("switch_op.wgsl") {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"in0", VIVID_PORT_GPU_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"in1", VIVID_PORT_GPU_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"in2", VIVID_PORT_GPU_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"in3", VIVID_PORT_GPU_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"texture", VIVID_PORT_GPU_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&index);
        out.push_back(&blend);
    }
};

VIVID_REGISTER(Switch)
