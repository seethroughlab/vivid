#include "operator_api/wgsl_filter.h"

struct Dither : vivid::WgslFilterBase {
    static constexpr const char* kName = "Dither";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = false;

    vivid::Param<int>   pattern  {"pattern",  1, {"Bayer 2x2", "Bayer 4x4", "Bayer 8x8"}};
    vivid::Param<float> levels   {"levels",   4.0f, 2.0f, 256.0f};
    vivid::Param<float> strength {"strength", 1.0f, 0.0f, 1.0f};

    Dither() : WgslFilterBase("dither.wgsl") {}

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&pattern);
        out.push_back(&levels);
        out.push_back(&strength);
    }
};

VIVID_REGISTER(Dither)
