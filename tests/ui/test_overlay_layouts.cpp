#include "ui/rendering/overlay_layouts.h"
#include <cstdio>
#include <cmath>
#include "test_helpers.h"

static bool nearly(float a, float b) {
    return std::fabs(a - b) < 0.0001f;
}

int main() {
    using namespace vivid::ui;

    auto e0 = compute_example_browser_layout(1280, 720, 1);
    auto e1 = compute_example_browser_layout(1280, 720, 1);
    check(nearly(e0.px, e1.px) && nearly(e0.py, e1.py) && nearly(e0.ph, e1.ph),
          "example layout deterministic for same input");

    auto p0 = compute_package_browser_layout(1280, 720, 10);
    auto p1 = compute_package_browser_layout(1280, 720, 10);
    check(nearly(p0.list_top, p1.list_top) && nearly(p0.status_y, p1.status_y),
          "package layout deterministic for same input");

    auto open_btn = compute_example_open_button_rect(e0, e0.list_top);
    check(overlay_contains(e0, open_btn.x + open_btn.w * 0.5f, open_btn.y + open_btn.h * 0.5f),
          "example open button center lies inside example panel");

    auto pkg_btn = compute_package_action_button_rect(p0, p0.list_top);
    check(overlay_contains(p0, pkg_btn.x + pkg_btn.w * 0.5f, pkg_btn.y + pkg_btn.h * 0.5f),
          "package action button center lies inside package panel");

    auto e_small = compute_example_browser_layout(900, 550, 20);
    auto e_large = compute_example_browser_layout(1700, 1100, 20);
    check(e_large.pw > e_small.pw || nearly(e_large.pw, e_small.pw),
          "example panel width is stable across window sizes");
    check(e_large.ph >= e_small.ph || nearly(e_large.ph, e_small.ph),
          "example panel height increases or saturates on larger windows");

    std::fprintf(stderr, "%s (%d failures)\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED",
                 failures);
    return failures == 0 ? 0 : 1;
}

