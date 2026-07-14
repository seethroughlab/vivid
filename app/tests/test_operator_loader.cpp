// Headless test for OperatorLoader: dlopen + ABI check + symbol resolution +
// descriptor validation, using GPU-free fixture dylibs (no wgpu device needed).
#include "gpu/operator_loader.h"
#include "test_helpers.h"

#include <cstdlib>
#include <string>

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

    return vivid::test::summary("test_operator_loader");
}
