#include "operator_api/wgsl_filter.h"

struct Halftone : vivid::WgslFilterBase {
    static constexpr const char* kName = "Halftone";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> dot_size {"dot_size", 8.0f, 1.0f, 50.0f};
    vivid::Param<float> angle    {"angle",    0.4f, 0.0f, 6.28318f};

    Halftone() : WgslFilterBase("halftone.wgsl") {}

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&dot_size);
        out.push_back(&angle);
    }
};

VIVID_REGISTER(Halftone)
