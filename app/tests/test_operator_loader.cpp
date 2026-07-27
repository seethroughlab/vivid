// Headless test for OperatorLoader: dlopen + ABI check + symbol resolution +
// descriptor validation, using GPU-free fixture dylibs (no wgpu device needed).
#include "gpu/operator_loader.h"
#include "gpu/operator_scan.h"   // load_and_register_operator_ex (structured register outcome)
#include "gpu/op_runtime.h"      // OpRegistry
#include "test_helpers.h"

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

int main() {
    using namespace vivid;

    // 1. A valid fixture loads; its descriptor + registration mode read across the boundary.
    {
        OperatorLoader L;
        CHECK(L.load(FIXTURE_OP_PATH));
        CHECK(L.is_loaded());
        CHECK(L.descriptor() != nullptr);
        CHECK(std::string(L.descriptor()->name) == "FixtureOp");
        CHECK(L.descriptor()->param_count == 2u);
        CHECK(L.registration_mode() == "legacy");  // bare macro exports no vivid_registration_mode
    }

    // 2. A bogus path → dlopen_failed (prior state untouched).
    {
        OperatorLoader L;
        CHECK(!L.load("/no/such/operator.dylib"));
        CHECK(L.last_error().code == "dlopen_failed");
        CHECK(!L.is_loaded());
    }

    // 3. A dylib missing the required entry points → rejected.
    {
        OperatorLoader L;
        CHECK(!L.load(FIXTURE_BAD_PATH));
        CHECK(L.last_error().code == "missing_required_symbols");
    }

    // 4. ABI compatibility. The runtime loads any operator in [MIN_LOADABLE, current], because the
    //    ABI only ever grows by APPENDING fields — an older operator still finds everything it knows
    //    at the same offset. (Demanding an exact match meant a purely additive bump orphaned every
    //    operator dylib a user had already installed; a non-additive change bumps MIN_LOADABLE.)
    {
        // A dylib NEWER than the runtime is rejected: it may expect fields we don't provide.
        setenv("VIVID_MOCK_RUNTIME_ABI", "1", 1);
        OperatorLoader L;
        CHECK(!L.load(FIXTURE_OP_PATH));
        CHECK(L.last_error().code == "abi_mismatch");
        unsetenv("VIVID_MOCK_RUNTIME_ABI");
    }
    {
        // An OLDER dylib (the fixture, built at the current ABI) on a newer runtime still loads,
        // so long as it is at/above MIN_LOADABLE.
        setenv("VIVID_MOCK_RUNTIME_ABI", "999", 1);
        OperatorLoader L;
        CHECK(L.load(FIXTURE_OP_PATH));
        unsetenv("VIVID_MOCK_RUNTIME_ABI");
    }

    // 5. The register wrapper surfaces the STRUCTURED load reason, not just "not registered". An
    //    ABI-mismatched dylib → RegisterResult{ok:false, error_key:"abi_mismatch"} (was lost as "").
    {
        setenv("VIVID_MOCK_RUNTIME_ABI", "1", 1);
        OpRegistry reg;
        std::vector<std::unique_ptr<OperatorLoader>> loaders;
        RegisterResult rr = load_and_register_operator_ex(FIXTURE_OP_PATH, reg, loaders);
        CHECK(!rr.ok);
        CHECK(rr.error_key == "abi_mismatch");
        CHECK(!rr.error_msg.empty());
        CHECK(!rr.shadowed);
        CHECK(load_and_register_operator(FIXTURE_OP_PATH, reg, loaders).empty());  // wrapper: still ""
        unsetenv("VIVID_MOCK_RUNTIME_ABI");
    }

    // 6. A clean load registers (ok + op_name, no error); a second load of the same name is SHADOWED
    //    (a non-error skip), distinct from a load failure.
    {
        OpRegistry reg;
        std::vector<std::unique_ptr<OperatorLoader>> loaders;
        RegisterResult rr = load_and_register_operator_ex(FIXTURE_OP_PATH, reg, loaders);
        CHECK(rr.ok);
        CHECK(rr.op_name == "FixtureOp");
        CHECK(rr.error_key.empty());

        std::vector<std::unique_ptr<OperatorLoader>> loaders2;
        RegisterResult rr2 = load_and_register_operator_ex(FIXTURE_OP_PATH, reg, loaders2);
        CHECK(!rr2.ok);
        CHECK(rr2.shadowed);
        CHECK(rr2.op_name == "FixtureOp");
        CHECK(rr2.error_key.empty());   // a shadow is not a load error
    }

    return vivid::test::summary("test_operator_loader");
}
