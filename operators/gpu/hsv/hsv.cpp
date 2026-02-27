#include "operator_api/wgsl_filter.h"

struct Hsv : vivid::WgslFilterBase {
    static constexpr const char* kName = "HSV";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> hue_shift  {"hue_shift",  0.0f, 0.0f, 360.0f};
    vivid::Param<float> saturation {"saturation", 0.0f, -1.0f, 1.0f};
    vivid::Param<float> value      {"value",      0.0f, -1.0f, 1.0f};

    Hsv() : WgslFilterBase("hsv.wgsl") {}

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&hue_shift);
        out.push_back(&saturation);
        out.push_back(&value);
    }
};

VIVID_REGISTER(Hsv)
