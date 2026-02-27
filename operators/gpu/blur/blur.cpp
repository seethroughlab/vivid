#include "operator_api/wgsl_filter.h"

struct Blur : vivid::WgslFilterBase {
    static constexpr const char* kName = "Blur";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> radius  {"radius",  5.0f, 0.0f, 50.0f};
    vivid::Param<float> quality {"quality", 4.0f, 1.0f, 16.0f};

    Blur() : WgslFilterBase("blur.wgsl") {}

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&radius);
        out.push_back(&quality);
    }
};

VIVID_REGISTER(Blur)
