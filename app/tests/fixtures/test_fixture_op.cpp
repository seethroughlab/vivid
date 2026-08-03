// Headless test fixture: a GPU-free operator compiled to a loadable module so the
// loader/adapter tests exercise the real dlopen/ABI/descriptor/mirror path without
// needing a wgpu device. FrameProcessable + VIVID_REGISTER → full extern "C" surface.
#include "operator_api/operator.h"
#include <array>

namespace {
VividPortDescriptor out_port(const char* name) {
    VividPortDescriptor p{};
    p.name = name; p.type = VIVID_PORT_TEXTURE; p.direction = VIVID_PORT_OUTPUT;
    p.value_type = VIVID_VALUE_TEXTURE; p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
    return p;
}
}  // namespace

struct FixtureOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "FixtureOp";
    // ADR-0046: exercise the operator-role export path end to end (dylib -> loader -> adapter -> host).
    static constexpr VividOperatorRole kRole = VIVID_OP_ROLE_RECIPE;
    static constexpr const char* kDisplayName = "Fixture Op";
    static constexpr const char* kSummary = "Headless test fixture operator.";
    static constexpr std::array<const char*, 2> kKeywords = {"test", "fixture"};
    vivid::Param<float> gain{"gain", 0.5f, 0.f, 2.f};
    vivid::Param<float> mix {"mix",  0.25f, 0.f, 1.f};
    void collect_params(std::vector<vivid::ParamBase*>& o) override { o.push_back(&gain); o.push_back(&mix); }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(out_port("out")); }
    void process_frame(const VividFrameContext*) override {}
};

VIVID_REGISTER(FixtureOp)

// ADR-0021/P3: exercise the file-drop ABI (vivid_file_drop_descriptor) through a real dylib.
static const char* const kFixtureDropExts[] = { ".foo", ".bar" };
static const VividFileDropHandlerDescriptor kFixtureDrop[] = {
    { "FixtureOp", kFixtureDropExts, 2, "file", 7, "Fixture drop handler" }
};
VIVID_FILE_DROP(kFixtureDrop)
