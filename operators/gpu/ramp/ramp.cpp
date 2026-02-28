#include "operator_api/wgsl_filter.h"

struct Ramp : vivid::WgslFilterBase {
    static constexpr const char* kName = "Ramp";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = false;

    vivid::Param<int>   mode       {"mode",       0, {"Linear", "Radial"}};
    vivid::Param<float> angle      {"angle",      0.0f,   0.0f, 360.0f};
    vivid::Param<float> offset_x   {"offset_x",   0.0f,  -1.0f, 1.0f};
    vivid::Param<float> offset_y   {"offset_y",   0.0f,  -1.0f, 1.0f};
    vivid::Param<float> scale      {"scale",      1.0f,   0.1f, 10.0f};
    vivid::Param<float> repeat     {"repeat",     1.0f,   0.1f, 10.0f};
    vivid::Param<float> hue_offset {"hue_offset", 0.0f,   0.0f, 1.0f};
    vivid::Param<float> hue_range  {"hue_range",  1.0f,   0.0f, 1.0f};
    vivid::Param<float> saturation {"saturation", 1.0f,   0.0f, 1.0f};
    vivid::Param<float> brightness {"brightness", 1.0f,   0.0f, 1.0f};

    Ramp() : WgslFilterBase("ramp.wgsl") {}

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"texture", VIVID_PORT_GPU_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        display_hint(offset_x, VIVID_DISPLAY_XY_PAD);
        display_hint(offset_y, VIVID_DISPLAY_XY_PAD);

        out.push_back(&mode);
        out.push_back(&angle);
        out.push_back(&offset_x);
        out.push_back(&offset_y);
        out.push_back(&scale);
        out.push_back(&repeat);
        out.push_back(&hue_offset);
        out.push_back(&hue_range);
        out.push_back(&saturation);
        out.push_back(&brightness);
    }
};

VIVID_REGISTER(Ramp)
