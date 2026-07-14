// Headless test for the pre-P1 session migration: the legacy VOp-int -> op-name
// map used when loading a saved visuals chain that predates op-by-name. (The full
// save/load round-trip needs Session/NodeGraph/GPU, so this covers the pure core.)
#include "persist.h"
#include "test_helpers.h"
#include <string>

int main() {
    using vivid::legacy_vop_name;
    CHECK(std::string(legacy_vop_name(0)) == "Plasma");
    CHECK(std::string(legacy_vop_name(1)) == "Video");
    CHECK(std::string(legacy_vop_name(2)) == "Feedback");
    CHECK(std::string(legacy_vop_name(3)) == "Blur");
    CHECK(std::string(legacy_vop_name(4)) == "Output");
    // Out-of-range -> Plasma (safe default, never a bad index).
    CHECK(std::string(legacy_vop_name(5))  == "Plasma");
    CHECK(std::string(legacy_vop_name(-1)) == "Plasma");
    CHECK(std::string(legacy_vop_name(999)) == "Plasma");

    // v3 (ADR-0016 / S5c): Composite's `mode` went from a 0..1 float the shader multiplied by 4
    // to a real enum index. A pre-v3 file's value must keep MEANING the same blend mode.
    using vivid::migrate_param_value;
    CHECK_NEAR(migrate_param_value(2, "Composite", "mode", 0.00f), 0.f, 1e-6);   // normal
    CHECK_NEAR(migrate_param_value(2, "Composite", "mode", 0.25f), 1.f, 1e-6);   // add (all 4 demos)
    CHECK_NEAR(migrate_param_value(2, "Composite", "mode", 0.50f), 2.f, 1e-6);   // multiply
    CHECK_NEAR(migrate_param_value(1, "Composite", "mode", 0.75f), 3.f, 1e-6);   // screen
    CHECK_NEAR(migrate_param_value(2, "Composite", "mode", 1.00f), 4.f, 1e-6);   // overlay
    // A v3 file is already an index: leave it alone (or every load would rescale it again).
    CHECK_NEAR(migrate_param_value(3, "Composite", "mode", 1.0f), 1.f, 1e-6);
    // Only that one param of that one op is touched.
    CHECK_NEAR(migrate_param_value(2, "Composite", "opacity", 0.25f), 0.25f, 1e-6);
    CHECK_NEAR(migrate_param_value(2, "Displace",  "mode",    0.25f), 0.25f, 1e-6);

    return vivid::test::summary("test_persist_chain_migration");
}
