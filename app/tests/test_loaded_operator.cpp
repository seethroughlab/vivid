// Headless test for LoadedOperator: the adapter must mirror a dlopen'd operator's
// descriptor losslessly so it flows through OpRegistry::create()/build_descriptor()
// identically to a built-in.
#include "gpu/operator_loader.h"
#include "gpu/loaded_operator.h"
#include "gpu/op_runtime.h"
#include "test_helpers.h"

#include <memory>
#include <string>
#include <vector>

int main() {
    using namespace vivid;

    OperatorLoader L;
    CHECK(L.load(FIXTURE_OP_PATH));
    const VividOperatorDescriptor* dyl = L.descriptor();
    CHECK(dyl != nullptr);

    // The adapter exposes the dylib's params/ports as synthetic ParamBase/ports.
    LoadedOperator op(&L);
    std::vector<ParamBase*> params; op.collect_params(params);
    CHECK(params.size() == 2);
    CHECK(std::string(params[0]->name) == "gain");
    CHECK(std::string(params[1]->name) == "mix");
    CHECK_NEAR(params[0]->default_value, 0.5f, 1e-6);
    CHECK_NEAR(params[1]->default_value, 0.25f, 1e-6);
    std::vector<VividPortDescriptor> ports; op.collect_ports(ports);
    CHECK(ports.size() == 1);
    CHECK(std::string(ports[0].name) == "out");
    CHECK(ports[0].direction == VIVID_PORT_OUTPUT);

    // Through OpRegistry: build_descriptor over the adapter reproduces the dylib's
    // descriptor field-for-field (name/params/ports) — the mirror is lossless.
    OpRegistry reg;
    OperatorLoader* raw = &L;
    reg.register_type("FixtureOp", [raw] {
        return std::unique_ptr<OperatorBase>(new LoadedOperator(raw));
    });
    std::vector<DescriptorValidationIssue> issues;
    auto inst = reg.create("FixtureOp", issues);
    CHECK(inst.has_value());
    CHECK(issues.empty());

    const VividOperatorDescriptor* built = reg.descriptor_for("FixtureOp");
    CHECK(built != nullptr);
    CHECK(std::string(built->name) == std::string(dyl->name));
    CHECK(built->param_count == dyl->param_count);
    for (uint32_t i = 0; i < built->param_count && i < dyl->param_count; ++i) {
        CHECK(std::string(built->params[i].name) == std::string(dyl->params[i].name));
        CHECK(built->params[i].type == dyl->params[i].type);
        CHECK_NEAR(built->params[i].default_value, dyl->params[i].default_value, 1e-6);
    }
    CHECK(built->port_count == dyl->port_count);
    for (uint32_t i = 0; i < built->port_count && i < dyl->port_count; ++i) {
        CHECK(std::string(built->ports[i].name) == std::string(dyl->ports[i].name));
        CHECK(built->ports[i].direction == dyl->ports[i].direction);
    }
    CHECK(built->has_process_gpu == 1);  // the adapter is a GpuProcessable

    return vivid::test::summary("test_loaded_operator");
}
