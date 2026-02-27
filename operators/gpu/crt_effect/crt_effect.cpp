#include "operator_api/wgsl_filter.h"

struct CRTEffect : vivid::WgslFilterBase {
    static constexpr const char* kName = "CRTEffect";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> curvature          {"curvature",          0.3f,   0.0f, 1.0f};
    vivid::Param<float> vignette           {"vignette",           0.3f,   0.0f, 1.0f};
    vivid::Param<float> scanline_intensity {"scanline_intensity", 0.4f,   0.0f, 1.0f};
    vivid::Param<float> bloom              {"bloom",              0.2f,   0.0f, 1.0f};
    vivid::Param<float> chromatic          {"chromatic",          0.005f, 0.0f, 0.05f};

    CRTEffect() : WgslFilterBase("crt_effect.wgsl") {}

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&curvature);
        out.push_back(&vignette);
        out.push_back(&scanline_intensity);
        out.push_back(&bloom);
        out.push_back(&chromatic);
    }
};

VIVID_REGISTER(CRTEffect)
