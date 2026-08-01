// Ph5 audit P2-05: a hosted plugin's authored param base must follow its param by STABLE id across a
// re-cache (rescan / restartComponent), not by compacted index — otherwise a rescan silently drops the
// user's tuned values. This test drives the pure helper both VST3 (float) and CLAP (double) handles use.
#include "audio/authored_base.h"
#include "test_helpers.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

using vivid::session::reapply_authored_base;

int main() {
    // Authored: id 10 -> 0.25, id 30 -> 0.90. (id 20 is left unauthored.)
    std::unordered_map<uint32_t, float> authored{ {10, 0.25f}, {30, 0.90f} };
    std::vector<float> base;
    std::vector<uint8_t> has;

    // A rescan REORDERS the table to [30, 10, 20]: authored values follow their id, not the old index.
    reapply_authored_base<uint32_t, float>(authored, { 30, 10, 20 }, base, has);
    CHECK(base.size() == 3 && has.size() == 3);
    CHECK(has[0] == 1 && base[0] == 0.90f);   // id 30
    CHECK(has[1] == 1 && base[1] == 0.25f);   // id 10
    CHECK(has[2] == 0 && base[2] == 0.f);     // id 20 was never authored

    // A rescan DROPS id 20 and ADDS a new id 40 (unauthored): survivors keep their authored values.
    reapply_authored_base<uint32_t, float>(authored, { 40, 30 }, base, has);
    CHECK(has[0] == 0);                        // id 40 is new
    CHECK(has[1] == 1 && base[1] == 0.90f);    // id 30 survives, still 0.90

    // A param whose id is gone from the plugin entirely simply doesn't reappear (no stale slot).
    reapply_authored_base<uint32_t, float>(authored, {}, base, has);
    CHECK(base.empty() && has.empty());

    // Empty authored map -> everything unauthored, no crash.
    std::unordered_map<uint32_t, float> none;
    reapply_authored_base<uint32_t, float>(none, { 1, 2 }, base, has);
    CHECK(base.size() == 2 && has[0] == 0 && has[1] == 0);

    // Double payload (the CLAP path): same behaviour, plain-unit values.
    std::unordered_map<uint32_t, double> authored_d{ {7, 440.0} };
    std::vector<double> based;
    std::vector<uint8_t> hasd;
    reapply_authored_base<uint32_t, double>(authored_d, { 1, 7 }, based, hasd);
    CHECK(hasd[0] == 0 && hasd[1] == 1 && based[1] == 440.0);

    return vivid::test::summary("test_authored_base");
}
