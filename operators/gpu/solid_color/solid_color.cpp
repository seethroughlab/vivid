#include "operator_api/wgsl_filter.h"

struct SolidColor : vivid::WgslFilterBase {
    static constexpr const char* kName = "SolidColor";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> r {"r", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> g {"g", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> b {"b", 1.0f, 0.0f, 1.0f};
    vivid::Param<float> a {"a", 1.0f, 0.0f, 1.0f};

    SolidColor() : WgslFilterBase("solid_color.wgsl") {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"texture", VIVID_PORT_GPU_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&r);
        out.push_back(&g);
        out.push_back(&b);
        out.push_back(&a);
    }
};

VIVID_REGISTER(SolidColor)
