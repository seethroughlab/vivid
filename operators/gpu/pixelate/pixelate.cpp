#include "operator_api/wgsl_filter.h"

struct Pixelate : vivid::WgslFilterBase {
    static constexpr const char* kName = "Pixelate";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> size_x {"size_x", 8.0f, 1.0f, 256.0f};
    vivid::Param<float> size_y {"size_y", 8.0f, 1.0f, 256.0f};

    Pixelate() : WgslFilterBase("pixelate.wgsl") {}

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        display_hint(size_x, VIVID_DISPLAY_XY_PAD);
        display_hint(size_y, VIVID_DISPLAY_XY_PAD);

        out.push_back(&size_x);
        out.push_back(&size_y);
    }
};

VIVID_REGISTER(Pixelate)
