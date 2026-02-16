/**
 * @file test_gui_preferences.cpp
 * @brief Preferences persistence regression tests
 *
 * Covers Bug 8: panel bounds persistence round-trip.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vivid/devtools/preferences.h>

using namespace vivid;
using Catch::Matchers::WithinAbs;

// =============================================================================
// Bug 8: Panel bounds persistence
// =============================================================================

TEST_CASE("Panel bounds round-trip", "[gui][preferences][bug8]") {
    auto& prefs = Preferences::instance();

    glm::vec4 bounds = {100.0f, 200.0f, 300.0f, 400.0f};
    prefs.setPanelBounds("test_panel_roundtrip", bounds);

    glm::vec4 loaded;
    REQUIRE(prefs.getPanelBounds("test_panel_roundtrip", loaded));
    REQUIRE_THAT(loaded.x, WithinAbs(100.0f, 0.01f));
    REQUIRE_THAT(loaded.y, WithinAbs(200.0f, 0.01f));
    REQUIRE_THAT(loaded.z, WithinAbs(300.0f, 0.01f));
    REQUIRE_THAT(loaded.w, WithinAbs(400.0f, 0.01f));
}

TEST_CASE("Unknown panel returns false", "[gui][preferences][bug8]") {
    auto& prefs = Preferences::instance();

    glm::vec4 loaded;
    REQUIRE_FALSE(prefs.getPanelBounds("nonexistent_panel_xyz", loaded));
}

TEST_CASE("Panel bounds overwrite previous value", "[gui][preferences]") {
    auto& prefs = Preferences::instance();

    prefs.setPanelBounds("test_overwrite", {10, 20, 30, 40});
    prefs.setPanelBounds("test_overwrite", {50, 60, 70, 80});

    glm::vec4 loaded;
    REQUIRE(prefs.getPanelBounds("test_overwrite", loaded));
    REQUIRE_THAT(loaded.x, WithinAbs(50.0f, 0.01f));
    REQUIRE_THAT(loaded.y, WithinAbs(60.0f, 0.01f));
}

TEST_CASE("Multiple panels stored independently", "[gui][preferences]") {
    auto& prefs = Preferences::instance();

    prefs.setPanelBounds("panel_a_test", {1, 2, 3, 4});
    prefs.setPanelBounds("panel_b_test", {5, 6, 7, 8});

    glm::vec4 a, b;
    REQUIRE(prefs.getPanelBounds("panel_a_test", a));
    REQUIRE(prefs.getPanelBounds("panel_b_test", b));

    REQUIRE_THAT(a.x, WithinAbs(1.0f, 0.01f));
    REQUIRE_THAT(b.x, WithinAbs(5.0f, 0.01f));
}
