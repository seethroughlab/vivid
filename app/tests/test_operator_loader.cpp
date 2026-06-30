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

    // 4. ABI mismatch — mock the runtime's expected ABI so the (correct) fixture is rejected.
    {
        setenv("VIVID_MOCK_RUNTIME_ABI", "999", 1);
        OperatorLoader L;
        CHECK(!L.load(FIXTURE_OP_PATH));
        CHECK(L.last_error().code == "abi_mismatch");
        unsetenv("VIVID_MOCK_RUNTIME_ABI");
    }

    return vivid::test::summary("test_operator_loader");
}
