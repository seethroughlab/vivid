#include "operator_api/wgsl_filter.h"

struct Gradient : vivid::WgslFilterBase {
    static constexpr const char* kName = "Gradient";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = false;

    vivid::Param<int>   mode     {"mode",     0, {"Linear", "Radial"}};
    vivid::Param<float> angle    {"angle",    0.0f,   0.0f, 360.0f};
    vivid::Param<float> center_x {"center_x", 0.5f,   0.0f, 1.0f};
    vivid::Param<float> center_y {"center_y", 0.5f,   0.0f, 1.0f};
    vivid::Param<float> scale    {"scale",    1.0f,   0.1f, 10.0f};
    vivid::Param<float> offset   {"offset",   0.0f,  -1.0f, 1.0f};

    Gradient() : WgslFilterBase("gradient.wgsl") {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"texture", VIVID_PORT_GPU_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&mode);
        out.push_back(&angle);
        out.push_back(&center_x);
        out.push_back(&center_y);
        out.push_back(&scale);
        out.push_back(&offset);
    }
};

VIVID_REGISTER(Gradient)
