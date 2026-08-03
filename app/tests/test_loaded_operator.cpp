// Headless test for LoadedOperator: the adapter must mirror a dlopen'd operator's
// descriptor losslessly so it flows through OpRegistry::create()/build_descriptor()
// identically to a built-in.
#include "gpu/operator_loader.h"
#include "gpu/loaded_operator.h"
#include "gpu/op_runtime.h"
#include "test_helpers.h"

#include <dlfcn.h>
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
    // Capability flags mirror the DYLIB descriptor, not the adapter's C++ interfaces (the adapter
    // implements all three so it can process any kind). FixtureOp is a frame operator, so the
    // built descriptor must report frame — NOT gpu (the pre-fix adapter mis-reported every loaded
    // op as gpu because it only inherited GpuProcessable).
    CHECK(built->has_process_gpu   == dyl->has_process_gpu);
    CHECK(built->has_process_audio == dyl->has_process_audio);
    CHECK(built->has_process_frame == dyl->has_process_frame);
    CHECK(built->has_process_frame == 1);
    CHECK(built->has_process_gpu == 0);

    // ADR-0046: the fixture declares kRole = RECIPE. It must travel end to end: the vivid_operator_role
    // export (dlsym'd by the loader) -> the dylib's own descriptor -> LoadedOperator::declared_operator_role
    // -> the host-built descriptor. Mirrors the v14 audio_role path.
    CHECK(L.operator_role() == VIVID_OP_ROLE_RECIPE);            // dlsym'd export
    CHECK(dyl->role == VIVID_OP_ROLE_RECIPE);                    // dylib's own descriptor (VIVID_REGISTER)
    CHECK(op.declared_operator_role() == VIVID_OP_ROLE_RECIPE);  // adapter forwards it
    CHECK(built->role == VIVID_OP_ROLE_RECIPE);                  // host descriptor records it

    // ADR-0050: the appended VividThumbnailContext.purpose (v17) crosses the dlopen boundary intact.
    // Drive draw_thumbnail through the adapter -> the dylib's vivid_draw_thumbnail -> FixtureOp, which
    // records the purpose; read it back via the fixture's test export (same image => shared static).
    {
        void* h = dlopen(FIXTURE_OP_PATH, RTLD_NOW | RTLD_LOCAL);
        CHECK(h != nullptr);
        auto last_purpose = reinterpret_cast<uint32_t (*)()>(dlsym(h, "vivid_test_last_preview_purpose"));
        auto abi_of       = reinterpret_cast<uint32_t (*)()>(dlsym(h, "vivid_abi_version"));
        CHECK(last_purpose != nullptr);
        CHECK(abi_of != nullptr && abi_of() == VIVID_OPERATOR_ABI_VERSION);   // fixture built at current ABI
        CHECK(last_purpose() == 0xFFFFFFFFu);                                 // not yet drawn

        VividThumbnailContext tc{};                 // zero-init => purpose defaults to DEFAULT
        CHECK(tc.purpose == VIVID_PREVIEW_DEFAULT);
        tc.purpose = VIVID_PREVIEW_AUDIO_NODE;
        op.draw_thumbnail(&tc);                     // adapter -> dlopen'd op (reads only ctx->purpose)
        CHECK(last_purpose() == VIVID_PREVIEW_AUDIO_NODE);   // the appended field arrived intact

        if (h) dlclose(h);
    }

    return vivid::test::summary("test_loaded_operator");
}
