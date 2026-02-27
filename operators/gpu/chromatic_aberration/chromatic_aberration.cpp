#include "operator_api/wgsl_filter.h"

struct ChromaticAberration : vivid::WgslFilterBase {
    static constexpr const char* kName = "ChromaticAberration";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> amount {"amount", 0.01f, 0.0f, 0.1f};
    vivid::Param<float> angle  {"angle",  0.0f,  0.0f, 360.0f};
    vivid::Param<float> radial {"radial", 0.0f,  0.0f, 1.0f};

    ChromaticAberration() : WgslFilterBase("chromatic_aberration.wgsl") {}

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&amount);
        out.push_back(&angle);
        out.push_back(&radial);
    }
};

VIVID_REGISTER(ChromaticAberration)
