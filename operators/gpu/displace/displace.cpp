#include "operator_api/wgsl_filter.h"

struct Displace : vivid::WgslFilterBase {
    static constexpr const char* kName = "Displace";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> amount {"amount", 0.1f, 0.0f, 1.0f};

    Displace() : WgslFilterBase("displace.wgsl") {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"source", VIVID_PORT_GPU_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"map", VIVID_PORT_GPU_TEXTURE, VIVID_PORT_INPUT});
        out.push_back({"texture", VIVID_PORT_GPU_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&amount);
    }
};

VIVID_REGISTER(Displace)
