// ADR-0021/P1 — the shader library view's pure geometry + fork-naming helpers. Header-only, so this
// runs headless with no GPU/registry. Guards the two things a bug would silently break: the modal's
// draw/hit-test geometry agreeing, and fork picking a name that is actually free.
#include "ui/shader_library_view.h"
#include "test_helpers.h"

#include <set>
#include <string>

using namespace vivid::ui;

namespace {

void test_geom_centered_and_clamped() {
    // Centered horizontally in the window; fixed top.
    const auto o = shader_view_geom(5, 1200);
    CHECK_NEAR(o.px + o.w * 0.5f, 600.f, 0.5f);
    CHECK(o.py == 84.f);
    // Height grows with row count.
    CHECK(o.vis == 5);
    CHECK(o.h > shader_view_geom(1, 1200).h);
    // Visible rows clamp at 14 no matter how many entries.
    CHECK(shader_view_geom(999, 1200).vis == 14);
    // At least one row even for an empty library (so the "none" line has somewhere to sit).
    CHECK(shader_view_geom(0, 1200).vis == 1);
}

void test_row_rects_inside_panel() {
    const auto o = shader_view_geom(6, 1000);
    const float ry = o.py + o.hdr;
    const auto r = shader_view_row(o.px, o.w, ry);
    // Both action rects sit within the panel's right edge and don't overlap.
    CHECK(r.open.x > o.px && r.open.x + r.open.w <= o.px + o.w);
    CHECK(r.fork.x > o.px && r.fork.x + r.fork.w <= o.px + o.w);
    CHECK(r.open.x + r.open.w <= r.fork.x);   // open is left of fork, no overlap
}

void test_fork_name_picks_free() {
    std::set<std::string> taken = { "Plasma", "Plasma2", "Plasma3" };
    auto is_taken = [&](const std::string& c) { return taken.count(c) > 0; };
    // Skips the occupied suffixes and lands on the first free one.
    CHECK(shader_fork_name("Plasma", is_taken) == "Plasma4");
    // A base with nothing taken forks to base2 (a "copy", not the original name).
    CHECK(shader_fork_name("Gradient", [](const std::string&) { return false; }) == "Gradient2");
    // Never returns the base itself (which would collide with the source op).
    CHECK(shader_fork_name("Tint", is_taken) != "Tint");
}

}  // namespace

int main() {
    test_geom_centered_and_clamped();
    test_row_rects_inside_panel();
    test_fork_name_picks_free();
    return vivid::test::summary("shader_library_view");
}
