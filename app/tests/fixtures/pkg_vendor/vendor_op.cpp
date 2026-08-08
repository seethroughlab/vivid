// ADR-0054 Stage 1 proof operator (GPU-free). It includes a VENDORED header that is
// only reachable through the manifest's `dependencies.vendor` include dir. If the
// package compiler didn't add that -I, this translation unit fails to compile — so a
// clean install→load of this op end to end proves vendored include dirs work.
#include "operator_api/operator.h"
#include <vendorlib/answer.h>   // resolved only via -I <pkg>/vendor/inc
#include <array>

struct VendorNoop : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "VendorNoop";
    static constexpr const char* kDisplayName = "Vendor Noop";
    static constexpr const char* kSummary = "Package op that uses a vendored header.";
    static constexpr std::array<const char*, 2> kKeywords = {"pkg", "vendor"};
    // Fold the vendored constant into a param range so it can't be optimized away.
    vivid::Param<float> x{"x", 0.f, 0.f, static_cast<float>(vendorlib::kVendorAnswer)};
    void collect_params(std::vector<vivid::ParamBase*>& o) override { o.push_back(&x); }
    void collect_ports(std::vector<VividPortDescriptor>&) override {}
    void process_frame(const VividFrameContext*) override {}
};

VIVID_REGISTER(VendorNoop)
