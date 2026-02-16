/**
 * @file test_gui_panels.cpp
 * @brief PanelManager UX regression tests
 *
 * Covers: visibility toggling (Bug 1), floating panel consistency (Bug 2),
 * input routing during content interaction (Bug 3), focus management, z-order.
 */

#include <catch2/catch_test_macros.hpp>
#include "gui_test_helpers.h"
#include <vivid/gui/panel_manager.h>

using namespace vivid;
using namespace vivid::test;

// =============================================================================
// Bug 1: Inspector visibility toggling
// =============================================================================

TEST_CASE("PanelManager visibility", "[gui][panels][bug1]") {
    PanelManager mgr;

    // Add a floating panel (like inspector), initially hidden
    mgr.addPanel(std::make_unique<TestPanel>(
        "inspector", PanelRole::Floating, glm::vec4{600, 100, 280, 400}, false));

    SECTION("showPanel makes hidden panel visible") {
        REQUIRE_FALSE(mgr.isPanelVisible("inspector"));
        mgr.showPanel("inspector");
        REQUIRE(mgr.isPanelVisible("inspector"));
    }

    SECTION("hidePanel makes panel invisible") {
        mgr.showPanel("inspector");
        REQUIRE(mgr.isPanelVisible("inspector"));
        mgr.hidePanel("inspector");
        REQUIRE_FALSE(mgr.isPanelVisible("inspector"));
    }

    SECTION("togglePanel flips state") {
        REQUIRE_FALSE(mgr.isPanelVisible("inspector"));
        mgr.togglePanel("inspector");
        REQUIRE(mgr.isPanelVisible("inspector"));
        mgr.togglePanel("inspector");
        REQUIRE_FALSE(mgr.isPanelVisible("inspector"));
    }

    SECTION("showPanel on already-visible panel does not re-steal focus") {
        // Add a second floating panel and focus it
        mgr.addPanel(std::make_unique<TestPanel>(
            "performance", PanelRole::Floating, glm::vec4{300, 100, 200, 300}, true));

        mgr.showPanel("inspector");       // inspector gets focus
        mgr.setFocus("performance");       // now performance has focus
        REQUIRE(mgr.focusedPanelId() == "performance");

        mgr.showPanel("inspector");        // already visible — should NOT steal focus
        REQUIRE(mgr.focusedPanelId() == "performance");
    }

    SECTION("hidePanel removes panel from floating z-order") {
        mgr.showPanel("inspector");

        // determineInputTarget should find inspector when mouse is over it
        auto input = makeInput({700, 200});
        REQUIRE(mgr.determineInputTarget(input) == "inspector");

        mgr.hidePanel("inspector");

        // After hiding, determineInputTarget should NOT find it
        REQUIRE(mgr.determineInputTarget(input).empty());
    }
}

// =============================================================================
// Bug 2: Floating panel consistency
// =============================================================================

TEST_CASE("Floating panel consistency", "[gui][panels][bug2]") {
    PanelManager mgr;

    mgr.addPanel(std::make_unique<TestPanel>(
        "inspector", PanelRole::Floating, glm::vec4{600, 100, 280, 400}, true));
    mgr.addPanel(std::make_unique<TestPanel>(
        "performance", PanelRole::Floating, glm::vec4{500, 150, 250, 350}, true));
    mgr.addPanel(std::make_unique<TestPanel>(
        "presets", PanelRole::Floating, glm::vec4{550, 120, 200, 300}, true));

    SECTION("all floating panels participate in z-order") {
        // Mouse in the overlap region — the last-added panel (presets) should be on top
        auto input = makeInput({650, 250});
        std::string target = mgr.determineInputTarget(input);
        REQUIRE(target == "presets");  // Last added = last in z-order = front
    }

    SECTION("setFocus on floating panel brings it to front") {
        // Initially: inspector, performance, presets (back to front)
        mgr.setFocus("inspector");  // Should bring inspector to front

        // Now inspector is on top in the overlap region
        auto input = makeInput({650, 250});
        std::string target = mgr.determineInputTarget(input);
        REQUIRE(target == "inspector");
    }

    SECTION("focus transfers between floating panels") {
        mgr.setFocus("inspector");
        REQUIRE(mgr.focusedPanelId() == "inspector");
        REQUIRE(mgr.getPanel("inspector")->isFocused());

        mgr.setFocus("performance");
        REQUIRE(mgr.focusedPanelId() == "performance");
        REQUIRE(mgr.getPanel("performance")->isFocused());
        REQUIRE_FALSE(mgr.getPanel("inspector")->isFocused());
    }
}

// =============================================================================
// Bug 3: Node drag stops when cursor crosses overlapping panel
// =============================================================================

TEST_CASE("Input routing preserves active content interaction", "[gui][panels][bug3]") {
    PanelManager mgr;

    // Background panel (like node graph) fills the screen
    auto bg = std::make_unique<TestPanel>(
        "nodegraph", PanelRole::Background, glm::vec4{0, 0, 1920, 1080}, true);
    auto* bgPtr = bg.get();
    mgr.addPanel(std::move(bg));

    // Floating panel (like inspector) overlapping part of background
    mgr.addPanel(std::make_unique<TestPanel>(
        "inspector", PanelRole::Floating, glm::vec4{600, 100, 280, 400}, true));

    SECTION("content-interacting panel wins over everything") {
        // Mouse is over the inspector's area
        auto input = makeInput({700, 200});

        // Without content interaction, inspector wins (floating panel on top)
        REQUIRE(mgr.determineInputTarget(input) == "inspector");

        // Simulate node graph drag in progress
        bgPtr->m_contentInteracting = true;

        // Now the background panel should win even though mouse is over inspector
        REQUIRE(mgr.determineInputTarget(input) == "nodegraph");
    }

    SECTION("after content interaction ends, normal routing resumes") {
        bgPtr->m_contentInteracting = true;
        auto input = makeInput({700, 200});
        REQUIRE(mgr.determineInputTarget(input) == "nodegraph");

        bgPtr->m_contentInteracting = false;
        REQUIRE(mgr.determineInputTarget(input) == "inspector");
    }
}

// =============================================================================
// Focus management
// =============================================================================

TEST_CASE("PanelManager focus", "[gui][panels]") {
    PanelManager mgr;

    mgr.addPanel(std::make_unique<TestPanel>(
        "panel_a", PanelRole::Floating, glm::vec4{100, 100, 200, 200}, true));
    mgr.addPanel(std::make_unique<TestPanel>(
        "panel_b", PanelRole::Floating, glm::vec4{400, 100, 200, 200}, true));

    SECTION("setFocus changes focusedPanelId") {
        mgr.setFocus("panel_a");
        REQUIRE(mgr.focusedPanelId() == "panel_a");
    }

    SECTION("setFocus defocuses previous panel") {
        mgr.setFocus("panel_a");
        REQUIRE(mgr.getPanel("panel_a")->isFocused());

        mgr.setFocus("panel_b");
        REQUIRE_FALSE(mgr.getPanel("panel_a")->isFocused());
        REQUIRE(mgr.getPanel("panel_b")->isFocused());
    }

    SECTION("setFocus with empty string clears focus") {
        mgr.setFocus("panel_a");
        mgr.setFocus("");
        REQUIRE(mgr.focusedPanelId().empty());
        REQUIRE_FALSE(mgr.getPanel("panel_a")->isFocused());
    }
}

// =============================================================================
// determineInputTarget comprehensive
// =============================================================================

TEST_CASE("determineInputTarget", "[gui][panels]") {
    PanelManager mgr;

    // Background panel
    mgr.addPanel(std::make_unique<TestPanel>(
        "bg", PanelRole::Background, glm::vec4{0, 40, 1920, 1040}, true));

    // Status bar
    mgr.addPanel(std::make_unique<TestPanel>(
        "status", PanelRole::StatusBar, glm::vec4{0, 0, 1920, 40}, true));

    // Two overlapping floating panels
    mgr.addPanel(std::make_unique<TestPanel>(
        "float_back", PanelRole::Floating, glm::vec4{500, 200, 300, 300}, true));
    mgr.addPanel(std::make_unique<TestPanel>(
        "float_front", PanelRole::Floating, glm::vec4{600, 250, 300, 300}, true));

    SECTION("mouse over floating panel returns that panel") {
        // Only float_back is here (not overlapping with float_front)
        auto input = makeInput({510, 210});
        REQUIRE(mgr.determineInputTarget(input) == "float_back");
    }

    SECTION("topmost floating panel wins when overlapping") {
        // In the overlap region
        auto input = makeInput({700, 350});
        REQUIRE(mgr.determineInputTarget(input) == "float_front");
    }

    SECTION("mouse over background panel (no floating above) returns background") {
        auto input = makeInput({100, 500});
        REQUIRE(mgr.determineInputTarget(input) == "bg");
    }

    SECTION("mouse over status bar returns status bar") {
        auto input = makeInput({500, 20});
        REQUIRE(mgr.determineInputTarget(input) == "status");
    }

    SECTION("mouse outside all panels returns empty") {
        // Hidden floating panel area but no visible content
        auto input = makeInput({-50, -50});
        REQUIRE(mgr.determineInputTarget(input).empty());
    }

    SECTION("hidden floating panel is skipped") {
        mgr.hidePanel("float_front");
        auto input = makeInput({700, 350});
        REQUIRE(mgr.determineInputTarget(input) == "float_back");
    }

    SECTION("floating panel hit area includes 8px padding") {
        // float_back bounds: {500, 200, 300, 300} → extends to [492, 808] x [192, 508]
        // Just inside the padding zone
        auto input = makeInput({495, 205});
        REQUIRE(mgr.determineInputTarget(input) == "float_back");

        // Just outside the padding zone
        auto outside = makeInput({490, 205});
        std::string target = mgr.determineInputTarget(outside);
        REQUIRE(target != "float_back");
    }
}
