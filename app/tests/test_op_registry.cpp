// Headless test for the in-process operator runtime (gpu/op_runtime). Uses
// GPU-free stand-in operators (process_gpu is a no-op) so it stays headless/
// portable; the real GPU ops are validated at app startup + exercised live.
#include "gpu/op_runtime.h"
#include "test_helpers.h"

#include <algorithm>
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

// A 2-input effect (Composite/Displace-shaped): 1 param, 2 texture inputs (A,B) + 1 output.
// The descriptor-driven port UI (node_graph.cpp op_in_count/op_in_port) keys off
// input_port_count to draw one independently-wireable stub per input, so this shape
// must report input_port_count == 2.
struct Fx2StandIn : vivid::OperatorBase, vivid::GpuProcessable {
    vivid::Param<float> mix{"mix", 0.5f, 0.f, 1.f};
    void collect_params(std::vector<vivid::ParamBase*>& out) override { out.push_back(&mix); }
    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        VividPortDescriptor a{}; a.name = "A"; a.type = VIVID_PORT_TEXTURE; a.direction = VIVID_PORT_INPUT;
        VividPortDescriptor b{}; b.name = "B"; b.type = VIVID_PORT_TEXTURE; b.direction = VIVID_PORT_INPUT;
        VividPortDescriptor o{}; o.name = "texture"; o.type = VIVID_PORT_TEXTURE; o.direction = VIVID_PORT_OUTPUT;
        out.push_back(a); out.push_back(b); out.push_back(o);
    }
    void process_gpu(const VividGpuContext*) override {}
};

// A generator with a FILE param (Image-shaped): default_string must be populated by
// build_descriptor so descriptor validation doesn't flag param_missing_default_string.
struct FileStandIn : vivid::OperatorBase, vivid::GpuProcessable {
    vivid::Param<vivid::FilePath> path{"file", "seed.png"};
    void collect_params(std::vector<vivid::ParamBase*>& out) override { out.push_back(&path); }
    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        VividPortDescriptor o{}; o.name = "texture"; o.type = VIVID_PORT_TEXTURE; o.direction = VIVID_PORT_OUTPUT;
        out.push_back(o);
    }
    void process_gpu(const VividGpuContext*) override {}
};

}  // namespace

int main() {
    vivid::OpRegistry reg;
    reg.register_type("Gen", [] { return std::make_unique<GenStandIn>(); });
    reg.register_type("Fx",  [] { return std::make_unique<FxStandIn>(); });
    reg.register_type("Fx2", [] { return std::make_unique<Fx2StandIn>(); });
    reg.register_type("Filey", [] { return std::make_unique<FileStandIn>(); });

    // Registry surface: names in registration order; has().
    auto names = reg.type_names();
    CHECK(names.size() == 4 && names[0] == "Gen" && names[1] == "Fx" && names[2] == "Fx2" && names[3] == "Filey");
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

    // Fx2: two texture inputs (drives the multi-port node UI) + one output.
    auto f2 = reg.create("Fx2", issues);
    CHECK(f2.has_value() && issues.empty());
    CHECK(f2->input_port_count == 2 && f2->output_port_count == 1);

    // Filey: a FILE param must carry a non-null default_string (== declared default) and
    // pass validation (no param_missing_default_string).
    auto fy = reg.create("Filey", issues);
    CHECK(fy.has_value() && issues.empty());
    const VividOperatorDescriptor* fd = reg.descriptor_for("Filey");
    CHECK(fd != nullptr && fd->param_count == 1);
    CHECK(fd->params[0].type == VIVID_PARAM_FILE);
    CHECK(fd->params[0].default_string != nullptr &&
          std::string(fd->params[0].default_string) == "seed.png");

    // Registry now lists four types in registration order.
    CHECK(reg.type_names().size() == 4);

    // Unknown type → no instance.
    CHECK(!reg.create("Bogus", issues).has_value());

    // sync_params writes resolved values in collect_params order.
    float vals[2] = { 0.9f, 1.5f };
    vivid::sync_params(*gi, vals, 2);
    CHECK(gi->param_ptrs[0]->value == 0.9f);
    CHECK(gi->param_ptrs[1]->value == 1.5f);

    // unregister_type retires a type (the project-scoped-operator teardown path): has() is
    // false, create() has no value, its cached descriptor is dropped, and it leaves type_names —
    // while the other types are untouched. Build Fx2's descriptor first, so we prove the cache is
    // cleared too (not just the entry).
    CHECK(reg.descriptor_for("Fx2") != nullptr);
    CHECK(reg.has("Fx2"));
    reg.unregister_type("Fx2");
    CHECK(!reg.has("Fx2"));
    CHECK(!reg.create("Fx2", issues).has_value());
    CHECK(reg.descriptor_for("Fx2") == nullptr);
    {
        auto ns = reg.type_names();
        CHECK(ns.size() == 3 && std::find(ns.begin(), ns.end(), "Fx2") == ns.end());
    }
    CHECK(reg.has("Gen") && reg.has("Fx") && reg.has("Filey"));   // siblings intact
    reg.unregister_type("Bogus");                                 // unknown name → harmless no-op
    CHECK(reg.type_names().size() == 3);
    // Re-registering the same name after an unregister works (a project re-open): the descriptor
    // rebuilds fresh from the new factory.
    reg.register_type("Fx2", [] { return std::make_unique<Fx2StandIn>(); });
    CHECK(reg.has("Fx2") && reg.descriptor_for("Fx2") != nullptr);

    return vivid::test::summary("test_op_registry");
}
