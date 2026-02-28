#include "operator_api/wgsl_filter.h"

struct Transform : vivid::WgslFilterBase {
    static constexpr const char* kName = "Transform";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> scale_x     {"scale_x",     1.0f, 0.01f, 10.0f};
    vivid::Param<float> scale_y     {"scale_y",     1.0f, 0.01f, 10.0f};
    vivid::Param<float> rotation    {"rotation",    0.0f, 0.0f, 360.0f};
    vivid::Param<float> translate_x {"translate_x", 0.0f, -1.0f, 1.0f};
    vivid::Param<float> translate_y {"translate_y", 0.0f, -1.0f, 1.0f};
    vivid::Param<float> pivot_x     {"pivot_x",     0.5f, 0.0f, 1.0f};
    vivid::Param<float> pivot_y     {"pivot_y",     0.5f, 0.0f, 1.0f};

    Transform() : WgslFilterBase("transform.wgsl") {}

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        display_hint(scale_x,     VIVID_DISPLAY_XY_PAD);
        display_hint(scale_y,     VIVID_DISPLAY_XY_PAD);
        display_hint(translate_x, VIVID_DISPLAY_XY_PAD);
        display_hint(translate_y, VIVID_DISPLAY_XY_PAD);
        display_hint(pivot_x,     VIVID_DISPLAY_XY_PAD);
        display_hint(pivot_y,     VIVID_DISPLAY_XY_PAD);

        out.push_back(&scale_x);
        out.push_back(&scale_y);
        out.push_back(&rotation);
        out.push_back(&translate_x);
        out.push_back(&translate_y);
        out.push_back(&pivot_x);
        out.push_back(&pivot_y);
    }
};

VIVID_REGISTER(Transform)
