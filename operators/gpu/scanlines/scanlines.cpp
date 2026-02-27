#include "operator_api/wgsl_filter.h"

struct Scanlines : vivid::WgslFilterBase {
    static constexpr const char* kName = "Scanlines";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> spacing   {"spacing",   4.0f, 2.0f, 100.0f};
    vivid::Param<float> thickness {"thickness", 0.5f, 0.0f, 1.0f};
    vivid::Param<float> intensity {"intensity", 0.5f, 0.0f, 1.0f};
    vivid::Param<bool>  vertical  {"vertical",  false};

    Scanlines() : WgslFilterBase("scanlines.wgsl") {}

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&spacing);
        out.push_back(&thickness);
        out.push_back(&intensity);
        out.push_back(&vertical);
    }
};

VIVID_REGISTER(Scanlines)
