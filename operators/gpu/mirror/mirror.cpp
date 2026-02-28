#include "operator_api/wgsl_filter.h"

struct Mirror : vivid::WgslFilterBase {
    static constexpr const char* kName = "Mirror";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = false;

    vivid::Param<int>   mode     {"mode",     0, {"Horizontal", "Vertical", "Quad", "Kaleidoscope"}};
    vivid::Param<float> segments {"segments", 6.0f, 2.0f, 32.0f};
    vivid::Param<float> angle    {"angle",    0.0f, 0.0f, 360.0f};
    vivid::Param<float> center_x {"center_x", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> center_y {"center_y", 0.5f, 0.0f, 1.0f};

    Mirror() : WgslFilterBase("mirror.wgsl") {}

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        display_hint(center_x, VIVID_DISPLAY_XY_PAD);
        display_hint(center_y, VIVID_DISPLAY_XY_PAD);

        out.push_back(&mode);
        out.push_back(&segments);
        out.push_back(&angle);
        out.push_back(&center_x);
        out.push_back(&center_y);
    }
};

VIVID_REGISTER(Mirror)
