// Headless test for P2.2′ operator discovery metadata: display_name/keywords/
// summary must reach the descriptor for BOTH a compiled-in op (register_op<T>
// captures static members) and a loaded dylib op (the macro populates the dylib
// descriptor; the scan-style registration carries it through the registry).
#include "gpu/op_runtime.h"
#include "gpu/operator_loader.h"
#include "gpu/loaded_operator.h"
#include "test_helpers.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace {
struct MetaOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kDisplayName = "Meta Op";
    static constexpr const char* kSummary = "An op with discovery metadata.";
    static constexpr std::array<const char*, 2> kKeywords = {"alpha", "beta"};
    vivid::Param<float> amount{"amount", 0.25f, 0.f, 1.f};
    void collect_params(std::vector<vivid::ParamBase*>& o) override { o.push_back(&amount); }
    void collect_ports(std::vector<VividPortDescriptor>&) override {}
    void process_frame(const VividFrameContext*) override {}
};
}  // namespace

int main() {
    using namespace vivid;

    // 1. Compiled-in path: register_op<T> lifts static metadata into the descriptor.
    {
        OpRegistry reg;
        register_op<MetaOp>(reg, "MetaOp");
        const VividOperatorDescriptor* d = reg.descriptor_for("MetaOp");
        CHECK(d != nullptr);
        CHECK(d->display_name && std::string(d->display_name) == "Meta Op");
        CHECK(d->summary && std::string(d->summary) == "An op with discovery metadata.");
        CHECK(d->keyword_count == 2u);
        CHECK(d->keywords && std::string(d->keywords[0]) == "alpha");
        CHECK(d->keywords && std::string(d->keywords[1]) == "beta");
    }

    // 2. Loaded-dylib path: the VIVID_REGISTER macro populated the dylib's descriptor,
    //    and the scan-style registration carries it into the registry's descriptor.
    {
        OperatorLoader L;
        CHECK(L.load(FIXTURE_OP_PATH));
        const VividOperatorDescriptor* dyl = L.descriptor();
        CHECK(dyl != nullptr);
        CHECK(dyl->display_name && std::string(dyl->display_name) == "Fixture Op");
        CHECK(dyl->summary && std::string(dyl->summary) == "Headless test fixture operator.");
        CHECK(dyl->keyword_count == 2u);

        OpMeta meta;
        if (dyl->display_name) meta.display_name = dyl->display_name;
        if (dyl->summary)      meta.summary = dyl->summary;
        for (uint32_t i = 0; i < dyl->keyword_count; ++i)
            if (dyl->keywords && dyl->keywords[i]) meta.keywords.emplace_back(dyl->keywords[i]);
        meta.has = true;

        OperatorLoader* raw = &L;
        OpRegistry reg;
        reg.register_type("FixtureOp",
            [raw] { return std::unique_ptr<OperatorBase>(new LoadedOperator(raw)); },
            std::move(meta));
        const VividOperatorDescriptor* d = reg.descriptor_for("FixtureOp");
        CHECK(d != nullptr);
        CHECK(d->display_name && std::string(d->display_name) == "Fixture Op");
        CHECK(d->keyword_count == 2u);
        CHECK(d->keywords && std::string(d->keywords[0]) == "test");
    }

    return vivid::test::summary("test_op_metadata");
}
