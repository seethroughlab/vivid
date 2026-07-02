// Headless tests for the launch-window sizing policy (app/src/app/window_prefs.*):
// compute_launch_rect is pure — first-launch fraction+cap+center, restore-clamp, and
// off-screen reposition — plus a save→load JSON round-trip via a temp path.
#include "app/window_prefs.h"
#include "test_helpers.h"

#include <filesystem>

using namespace vivid;

static void test_first_launch_laptop() {
    // No saved prefs -> 90% of the work area, centered, under the caps.
    WindowPrefs none;
    LaunchRect r = compute_launch_rect(0, 0, 1512, 944, none, kLaunchMaxW, kLaunchMaxH, kLaunchFraction);
    CHECK(r.w == 1361);   // round(0.90 * 1512)
    CHECK(r.h == 850);    // round(0.90 * 944)
    CHECK(r.x == (1512 - 1361) / 2);
    CHECK(r.y == (944 - 850) / 2);
}

static void test_first_launch_superwide_capped() {
    // A superwide: width hits the cap, height is under it. Not the whole screen.
    WindowPrefs none;
    LaunchRect r = compute_launch_rect(0, 0, 3440, 1440, none, kLaunchMaxW, kLaunchMaxH, kLaunchFraction);
    CHECK(r.w == kLaunchMaxW);   // 0.90*3440 = 3096 -> capped to 2200
    CHECK(r.h == 1296);          // round(0.90 * 1440), under the 1350 cap
    CHECK(r.w < 3440 && r.h <= 1440);
    CHECK(r.x == (3440 - kLaunchMaxW) / 2);
}

static void test_restore_clamped_to_monitor() {
    // A remembered size bigger than the current (smaller) monitor is clamped to fit.
    WindowPrefs p; p.w = 5000; p.h = 3000; p.has_size = true;
    LaunchRect r = compute_launch_rect(0, 0, 1512, 944, p, kLaunchMaxW, kLaunchMaxH, kLaunchFraction);
    CHECK(r.w == 1512);
    CHECK(r.h == 944);
    CHECK(r.x == 0 && r.y == 0);   // fills the work area, origin at its corner
}

static void test_restore_exact_when_fits() {
    // A remembered size + position that fits is restored verbatim.
    WindowPrefs p; p.w = 1200; p.h = 800; p.x = 100; p.y = 60; p.has_size = true; p.has_pos = true;
    LaunchRect r = compute_launch_rect(0, 0, 1920, 1200, p, kLaunchMaxW, kLaunchMaxH, kLaunchFraction);
    CHECK(r.w == 1200 && r.h == 800);
    CHECK(r.x == 100 && r.y == 60);
}

static void test_offscreen_position_recentered() {
    // A saved position from a now-absent monitor is off this work area -> re-center.
    WindowPrefs p; p.w = 1200; p.h = 800; p.x = 4000; p.y = 3000; p.has_size = true; p.has_pos = true;
    LaunchRect r = compute_launch_rect(0, 0, 1920, 1200, p, kLaunchMaxW, kLaunchMaxH, kLaunchFraction);
    CHECK(r.w == 1200 && r.h == 800);
    CHECK(r.x == (1920 - 1200) / 2);
    CHECK(r.y == (1200 - 800) / 2);
}

static void test_workarea_origin_offset() {
    // A work area not at (0,0) (e.g. a second monitor) centers within its own bounds.
    WindowPrefs none;
    LaunchRect r = compute_launch_rect(1920, 0, 1000, 1000, none, kLaunchMaxW, kLaunchMaxH, kLaunchFraction);
    CHECK(r.w == 900 && r.h == 900);   // 0.90 * 1000
    CHECK(r.x == 1920 + (1000 - 900) / 2);
}

static void test_save_load_roundtrip() {
    const std::string path = (std::filesystem::temp_directory_path() / "vivid_window_prefs_test.json").string();
    std::error_code ec; std::filesystem::remove(path, ec);

    // A missing file -> no remembered size.
    CHECK(!load_window_prefs(path).has_size);

    WindowPrefs w; w.w = 1440; w.h = 900; w.x = 40; w.y = 25;
    CHECK(save_window_prefs(w, path));
    WindowPrefs got = load_window_prefs(path);
    CHECK(got.has_size && got.has_pos);
    CHECK(got.w == 1440 && got.h == 900 && got.x == 40 && got.y == 25);

    std::filesystem::remove(path, ec);
}

int main() {
    test_first_launch_laptop();
    test_first_launch_superwide_capped();
    test_restore_clamped_to_monitor();
    test_restore_exact_when_fits();
    test_offscreen_position_recentered();
    test_workarea_origin_offset();
    test_save_load_roundtrip();
    return vivid::test::summary("test_window_prefs");
}
