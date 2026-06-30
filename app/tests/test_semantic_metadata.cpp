// Headless test for the semantic-metadata contract (P4.4). Builds operator descriptors
// through the real register_op/build_descriptor path (wgpu-free) and asserts the
// vocabulary checker accepts conformant semantic_* hints + flags off-vocabulary ones.
#include "gpu/op_runtime.h"
#include "operator_api/semantic_vocab.h"
#include "test_helpers.h"

#include <string>
#include <vector>

namespace {
// Conformant: a frequency param (tag/shape/unit all in-vocabulary) + the standard
// analysis output ports (semantic_tag "analysis").
struct GoodOp : vivid::OperatorBase, vivid::FrameProcessable {
    vivid::Param<float> cutoff{"cutoff", 1000.f, 20.f, 20000.f};
    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        vivid::semantic_tag(cutoff, "frequency_hz");
        vivid::semantic_shape(cutoff, "scalar");
        vivid::semantic_unit(cutoff, "Hz");
        o.push_back(&cutoff);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { vivid::append_analysis_ports(o); }
    void process_frame(const VividFrameContext*) override {}
};

// Off-vocabulary: a made-up tag + a bogus unit. Should produce issues.
struct BadOp : vivid::OperatorBase, vivid::FrameProcessable {
    vivid::Param<float> amt{"amt", 0.f, 0.f, 1.f};
    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        vivid::semantic_tag(amt, "made_up_tag");
        vivid::semantic_unit(amt, "furlongs");
        o.push_back(&amt);
    }
    void collect_ports(std::vector<VividPortDescriptor>&) override {}
    void process_frame(const VividFrameContext*) override {}
};
}  // namespace

int main() {
    using namespace vivid;

    // Direct vocabulary checks.
    CHECK(semantic_value_ok(semantic_tags(), nullptr));          // unset OK
    CHECK(semantic_value_ok(semantic_tags(), ""));               // empty OK
    CHECK(semantic_value_ok(semantic_tags(), "frequency_hz"));   // known OK
    CHECK(semantic_value_ok(semantic_tags(), "x_my_custom"));    // extension OK
    CHECK(!semantic_value_ok(semantic_tags(), "not_a_tag"));     // unknown rejected
    CHECK(semantic_value_ok(semantic_units(), "Hz"));
    CHECK(!semantic_value_ok(semantic_units(), "furlongs"));
    // "analysis" (the one tag our real operators actually declare) is in-vocabulary.
    CHECK(semantic_value_ok(semantic_tags(), "analysis"));

    OpRegistry reg;
    register_op<GoodOp>(reg, "GoodOp");
    register_op<BadOp>(reg, "BadOp");

    const VividOperatorDescriptor* good = reg.descriptor_for("GoodOp");
    CHECK(good != nullptr);
    auto good_issues = validate_semantic_metadata(*good);
    CHECK(good_issues.empty());

    const VividOperatorDescriptor* bad = reg.descriptor_for("BadOp");
    CHECK(bad != nullptr);
    auto bad_issues = validate_semantic_metadata(*bad);
    CHECK(bad_issues.size() == 2);   // the bogus tag + the bogus unit

    return vivid::test::summary("test_semantic_metadata");
}
