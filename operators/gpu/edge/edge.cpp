#include "operator_api/wgsl_filter.h"

struct Edge : vivid::WgslFilterBase {
    static constexpr const char* kName = "Edge";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> strength  {"strength",  1.0f, 0.0f, 10.0f};
    vivid::Param<float> threshold {"threshold", 0.0f, 0.0f, 1.0f};
    vivid::Param<bool>  invert    {"invert",    false};

    Edge() : WgslFilterBase("edge.wgsl") {}

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&strength);
        out.push_back(&threshold);
        out.push_back(&invert);
    }
};

VIVID_REGISTER(Edge)
