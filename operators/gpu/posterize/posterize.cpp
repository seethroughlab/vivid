#include "operator_api/wgsl_filter.h"

struct Posterize : vivid::WgslFilterBase {
    static constexpr const char* kName = "Posterize";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> levels {"levels", 8.0f, 2.0f, 256.0f};

    Posterize() : WgslFilterBase("posterize.wgsl") {}

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&levels);
    }
};

VIVID_REGISTER(Posterize)
