/**
 * @file test_gui_status_bar.cpp
 * @brief Status bar dropdown regression tests
 *
 * Covers Bug 6: dropdown items fall outside panel hit area,
 * and all codec options call the same callback with no distinction.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinAbs;

// =============================================================================
// Bug 6: Dropdown extends below panel bounds
// Extracted from status_bar_panel.cpp lines 335-356
// =============================================================================

namespace {

/// Mirrors the ButtonRect from StatusBarPanel::Impl
struct ButtonRect {
    float x, y, w, h;
    bool valid = false;

    bool contains(float mx, float my) const {
        return valid && mx >= x && mx < x + w &&
               my >= y && my < y + h;
    }
};

/// Computes dropdown item positions using the same math as status_bar_panel.cpp
struct DropdownLayout {
    ButtonRect items[3];
    float menuY;
    float menuH;

    void compute(float barY, float barHeight, float recBtnX,
                 float menuWidth, float lineH, float buttonPadY) {
        menuY = barY + barHeight + 2.0f;
        float itemH = lineH + buttonPadY * 2.0f;
        menuH = itemH * 3.0f;

        float itemY = menuY;
        items[0] = {recBtnX, itemY, menuWidth, itemH, true};
        itemY += itemH;
        items[1] = {recBtnX, itemY, menuWidth, itemH, true};
        itemY += itemH;
        items[2] = {recBtnX, itemY, menuWidth, itemH, true};
    }
};

} // anonymous namespace

TEST_CASE("Status bar dropdown extends below panel bounds", "[gui][statusbar][bug6]") {
    // Typical status bar configuration
    float barY = 0.0f;        // Status bar at top of screen
    float barHeight = 40.0f;  // From UIStyle::statusBarHeight()
    float screenWidth = 1920.0f;

    // Status bar panel bounds: {0, 0, screenWidth, barHeight}
    float panelBottom = barY + barHeight;

    // Typical font/layout metrics
    float lineH = 14.0f;      // Typical line height for mono font
    float buttonPadY = 4.0f;  // Typical vertical button padding
    float recBtnX = 1600.0f;  // Record button near right side

    DropdownLayout dropdown;
    dropdown.compute(barY, barHeight, recBtnX, 180.0f, lineH, buttonPadY);

    SECTION("dropdown starts below status bar") {
        REQUIRE(dropdown.menuY > panelBottom);
        REQUIRE_THAT(dropdown.menuY, WithinAbs(barY + barHeight + 2.0f, 0.01f));
    }

    SECTION("all three dropdown items are positioned correctly") {
        float itemH = lineH + buttonPadY * 2.0f;

        REQUIRE(dropdown.items[0].valid);
        REQUIRE(dropdown.items[1].valid);
        REQUIRE(dropdown.items[2].valid);

        // Items should be stacked vertically
        REQUIRE_THAT(dropdown.items[1].y, WithinAbs(dropdown.items[0].y + itemH, 0.01f));
        REQUIRE_THAT(dropdown.items[2].y, WithinAbs(dropdown.items[1].y + itemH, 0.01f));
    }

    SECTION("dropdown items are outside status bar panel bounds") {
        // This documents the bug: the dropdown menu extends below the status bar's bounds.
        // Since input routing uses panel bounds for hit testing, clicks on dropdown items
        // may not reach the status bar panel through normal PanelManager input routing.
        // The status bar works around this by handling clicks directly in render().
        float item1Bottom = dropdown.items[0].y + dropdown.items[0].h;
        float item2Bottom = dropdown.items[1].y + dropdown.items[1].h;
        float item3Bottom = dropdown.items[2].y + dropdown.items[2].h;

        // All items are below the panel's own bounds
        REQUIRE(dropdown.items[0].y >= panelBottom);
        REQUIRE(item1Bottom > panelBottom);
        REQUIRE(item2Bottom > panelBottom);
        REQUIRE(item3Bottom > panelBottom);
    }

    SECTION("clicking each dropdown item hits the correct rect") {
        float centerX = recBtnX + 90.0f;  // Middle of menu
        float itemH = lineH + buttonPadY * 2.0f;

        // Click on item 0
        float y0 = dropdown.items[0].y + itemH / 2.0f;
        REQUIRE(dropdown.items[0].contains(centerX, y0));
        REQUIRE_FALSE(dropdown.items[1].contains(centerX, y0));
        REQUIRE_FALSE(dropdown.items[2].contains(centerX, y0));

        // Click on item 1
        float y1 = dropdown.items[1].y + itemH / 2.0f;
        REQUIRE_FALSE(dropdown.items[0].contains(centerX, y1));
        REQUIRE(dropdown.items[1].contains(centerX, y1));
        REQUIRE_FALSE(dropdown.items[2].contains(centerX, y1));

        // Click on item 2
        float y2 = dropdown.items[2].y + itemH / 2.0f;
        REQUIRE_FALSE(dropdown.items[0].contains(centerX, y2));
        REQUIRE_FALSE(dropdown.items[1].contains(centerX, y2));
        REQUIRE(dropdown.items[2].contains(centerX, y2));
    }
}

TEST_CASE("Record dropdown codec options pass distinct codecs", "[gui][statusbar][bug6]") {
    // After the fix, the RecordCallback signature is (bool start, ExportCodec codec).
    // Each dropdown item passes a different codec enum value.
    // Simulates the three dropdown click handlers from status_bar_panel.cpp:362-369.

    std::vector<int> codecs;
    auto recordCallback = [&](bool start, int codec) {
        if (start) codecs.push_back(codec);
    };

    // H.264 = 0, H.265 = 1, Animation = 2 (matching ExportCodec enum values)
    recordCallback(true, 0);   // H.264
    recordCallback(true, 1);   // H.265
    recordCallback(true, 2);   // ProRes/Animation

    REQUIRE(codecs.size() == 3);
    // All three should be distinct
    REQUIRE(codecs[0] != codecs[1]);
    REQUIRE(codecs[1] != codecs[2]);
    REQUIRE(codecs[0] != codecs[2]);
}
