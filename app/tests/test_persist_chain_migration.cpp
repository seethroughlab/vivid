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
    return vivid::test::summary("test_persist_chain_migration");
}
