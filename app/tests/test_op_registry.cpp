// Headless test for the in-process operator runtime (gpu/op_runtime). Uses
// GPU-free stand-in operators (process_gpu is a no-op) so it stays headless/
// portable; the real GPU ops are validated at app startup + exercised live.
#include "gpu/op_runtime.h"
#include "test_helpers.h"

#include <memory>
#include <string>
#include <vector>

namespace {

// A generator-shaped op: 2 float params, 1 texture output, no input.
struct GenStandIn : vivid::OperatorBase, vivid::GpuProcessable {
    vivid::Param<float> alpha{"alpha", 0.3f, 0.f, 1.f};
    vivid::Param<float> beta {"beta",  0.7f, 0.f, 2.f};
    void collect_params(std::vector<vivid::ParamBase*>& out) override { out.push_back(&alpha); out.push_back(&beta); }
    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        VividPortDescriptor o{}; o.name = "texture"; o.type = VIVID_PORT_TEXTURE; o.direction = VIVID_PORT_OUTPUT;
        out.push_back(o);
    }
    void process_gpu(const VividGpuContext*) override {}
};

// An effect-shaped op: 1 param, 1 texture input + 1 texture output.
struct FxStandIn : vivid::OperatorBase, vivid::GpuProcessable {
    vivid::Param<float> amount{"amount", 0.5f, 0.f, 1.f};
    void collect_params(std::vector<vivid::ParamBase*>& out) override { out.push_back(&amount); }
    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        VividPortDescriptor in{}; in.name = "input";   in.type = VIVID_PORT_TEXTURE; in.direction = VIVID_PORT_INPUT;
        VividPortDescriptor o{};  o.name  = "texture";  o.type  = VIVID_PORT_TEXTURE; o.direction  = VIVID_PORT_OUTPUT;
        out.push_back(in); out.push_back(o);
    }
    void process_gpu(const VividGpuContext*) override {}
};

}  // namespace

int main() {
    vivid::OpRegistry reg;
    reg.register_type("Gen", [] { return std::make_unique<GenStandIn>(); });
    reg.register_type("Fx",  [] { return std::make_unique<FxStandIn>(); });

    // Registry surface: names in registration order; has().
    auto names = reg.type_names();
    CHECK(names.size() == 2 && names[0] == "Gen" && names[1] == "Fx");
    CHECK(reg.has("Gen") && !reg.has("Bogus"));

    std::vector<vivid::DescriptorValidationIssue> issues;

    // Create Gen: param metadata in collect_params order, ports counted, valid.
    auto gi = reg.create("Gen", issues);
    CHECK(gi.has_value());
    CHECK(issues.empty());
    CHECK(gi->param_ptrs.size() == 2);
    CHECK(std::string(gi->param_ptrs[0]->name) == "alpha");
    CHECK(std::string(gi->param_ptrs[1]->name) == "beta");
    CHECK(gi->output_port_count == 1 && gi->input_port_count == 0);

    // descriptor_for reflects the same (built once, cached, stable).
    const VividOperatorDescriptor* d = reg.descriptor_for("Gen");
    CHECK(d != nullptr);
    CHECK(d->param_count == 2 && d->port_count == 1 && d->has_process_gpu == 1);
    CHECK(std::string(d->params[0].name) == "alpha");
    CHECK(d->params[1].max_value == 2.f);
    CHECK(d->multiplicity_behavior == VIVID_MULTIPLICITY_SCALAR_ONLY);

    // Fx: input + output ports.
    auto fi = reg.create("Fx", issues);
    CHECK(fi.has_value() && issues.empty());
    CHECK(fi->input_port_count == 1 && fi->output_port_count == 1);

    // Unknown type → no instance.
    CHECK(!reg.create("Bogus", issues).has_value());

    // sync_params writes resolved values in collect_params order.
    float vals[2] = { 0.9f, 1.5f };
    vivid::sync_params(*gi, vals, 2);
    CHECK(gi->param_ptrs[0]->value == 0.9f);
    CHECK(gi->param_ptrs[1]->value == 1.5f);

    return vivid::test::summary("test_op_registry");
}
