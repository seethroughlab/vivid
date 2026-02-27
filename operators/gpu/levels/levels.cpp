#include "operator_api/wgsl_filter.h"

struct Levels : vivid::WgslFilterBase {
    static constexpr const char* kName = "Levels";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> brightness {"brightness", 0.0f, -1.0f, 1.0f};
    vivid::Param<float> contrast   {"contrast",   1.0f,  0.0f, 3.0f};
    vivid::Param<float> gamma      {"gamma",      1.0f,  0.1f, 5.0f};

    Levels() : WgslFilterBase("levels.wgsl") {}

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&brightness);
        out.push_back(&contrast);
        out.push_back(&gamma);
    }
};

VIVID_REGISTER(Levels)
