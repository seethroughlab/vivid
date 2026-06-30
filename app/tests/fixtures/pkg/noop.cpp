// Headless test package operator (GPU-free) — compiled at test time by the package
// compiler, then loaded, to prove the package compile→load pipeline end to end.
#include "operator_api/operator.h"
#include <array>

struct PkgNoop : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "PkgNoop";
    static constexpr const char* kDisplayName = "Pkg Noop";
    static constexpr const char* kSummary = "Package-compiled test operator.";
    static constexpr std::array<const char*, 2> kKeywords = {"pkg", "test"};
    vivid::Param<float> x{"x", 0.f, 0.f, 1.f};
    void collect_params(std::vector<vivid::ParamBase*>& o) override { o.push_back(&x); }
    void collect_ports(std::vector<VividPortDescriptor>&) override {}
    void process_frame(const VividFrameContext*) override {}
};

VIVID_REGISTER(PkgNoop)
