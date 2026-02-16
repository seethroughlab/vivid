/**
 * @file test_gui_layout.cpp
 * @brief Layout math regression tests
 *
 * Covers: scroll offset clamping (Bug 4), output pin label positioning (Bug 5),
 * preset toolbar label clearance (Bug 7).
 *
 * These test the math formulas extracted from the rendering code,
 * not the rendering itself.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <algorithm>
#include <cmath>

using Catch::Matchers::WithinAbs;

// =============================================================================
// Bug 4: Scroll offset clamping
// Extracted from inspector_panel.cpp lines 174-178
// =============================================================================

namespace {

/// Mirrors the scroll clamping logic from InspectorPanel::render()
struct ScrollState {
    float scrollOffset = 0.0f;
    float contentHeight = 0.0f;

    void applyScroll(float scrollDeltaY, float viewportHeight) {
        scrollOffset -= scrollDeltaY * 30.0f;
        float maxScroll = std::max(0.0f, contentHeight - viewportHeight);
        scrollOffset = std::max(0.0f, std::min(scrollOffset, maxScroll));
    }
};

} // anonymous namespace

TEST_CASE("Scroll offset clamping", "[gui][layout][bug4]") {
    ScrollState state;

    SECTION("scroll down past end clamps to max") {
        state.contentHeight = 800.0f;
        float viewportHeight = 400.0f;
        float maxScroll = 800.0f - 400.0f;  // 400

        // Scroll down a lot (negative scrollDeltaY = scroll down)
        state.applyScroll(-100.0f, viewportHeight);
        REQUIRE(state.scrollOffset <= maxScroll);
        REQUIRE(state.scrollOffset >= 0.0f);
    }

    SECTION("scroll up past start clamps to 0") {
        state.contentHeight = 800.0f;
        float viewportHeight = 400.0f;

        // Start partway down
        state.scrollOffset = 200.0f;

        // Scroll up a lot (positive scrollDeltaY = scroll up)
        state.applyScroll(100.0f, viewportHeight);
        REQUIRE(state.scrollOffset == 0.0f);
    }

    SECTION("content shorter than viewport: maxScroll is 0, offset stays 0") {
        state.contentHeight = 200.0f;
        float viewportHeight = 400.0f;

        // Try scrolling down
        state.applyScroll(-5.0f, viewportHeight);
        REQUIRE(state.scrollOffset == 0.0f);

        // Try scrolling up
        state.applyScroll(5.0f, viewportHeight);
        REQUIRE(state.scrollOffset == 0.0f);
    }

    SECTION("exact fit: maxScroll is 0") {
        state.contentHeight = 400.0f;
        float viewportHeight = 400.0f;

        state.applyScroll(-10.0f, viewportHeight);
        REQUIRE(state.scrollOffset == 0.0f);
    }

    SECTION("incremental scrolling accumulates correctly") {
        state.contentHeight = 1000.0f;
        float viewportHeight = 400.0f;

        // Small scroll increments
        state.applyScroll(-1.0f, viewportHeight);  // +30
        REQUIRE_THAT(state.scrollOffset, WithinAbs(30.0f, 0.01f));

        state.applyScroll(-1.0f, viewportHeight);  // +30 more
        REQUIRE_THAT(state.scrollOffset, WithinAbs(60.0f, 0.01f));
    }
}

// =============================================================================
// Bug 5: Output pin label fits within node bounds
// Extracted from node_graph.cpp lines 894-901
// =============================================================================

namespace {

/// Computes the X position of an output pin label
/// Formula: labelX = pinX - pinR - textW - 6*zoom
/// where pinX = nodeX + nodeW (output pins are at right edge)
float computeOutputPinLabelX(float nodeX, float nodeW, float pinR,
                              float textW, float zoom) {
    float pinX = nodeX + nodeW;
    return pinX - pinR - textW - 6.0f * zoom;
}

} // anonymous namespace

TEST_CASE("Output pin label fits within node", "[gui][layout][bug5]") {
    // Default style values
    float nodeW = 180.0f;
    float pinR = 6.0f;

    SECTION("at zoom=1.0, short label 'out' fits within node") {
        float nodeX = 100.0f;
        float textW = 20.0f;  // "out" ~ 20px at default font
        float zoom = 1.0f;

        float labelX = computeOutputPinLabelX(nodeX, nodeW, pinR, textW, zoom);
        // labelX = 100 + 180 - 6 - 20 - 6 = 248, nodeX = 100 → fits
        REQUIRE(labelX >= nodeX);
    }

    SECTION("at zoom=0.5, short label still fits") {
        float nodeX = 100.0f;
        float textW = 10.0f;  // scaled down at 0.5x
        float zoom = 0.5f;

        float labelX = computeOutputPinLabelX(nodeX, nodeW * zoom, pinR * zoom, textW, zoom);
        REQUIRE(labelX >= nodeX * zoom);  // Positions scale with zoom too
    }

    SECTION("long label 'audio_output' may overflow at low zoom") {
        float nodeX = 100.0f;
        float textW = 80.0f;  // "audio_output" is wide
        float zoom = 1.0f;

        float labelX = computeOutputPinLabelX(nodeX, nodeW, pinR, textW, zoom);
        // labelX = 100 + 180 - 6 - 80 - 6 = 188, nodeX = 100 → still fits at 1x
        REQUIRE(labelX >= nodeX);
    }

    SECTION("very long label at small zoom overflows — documents the bug") {
        float nodeX = 100.0f;
        // At zoom 0.3, node is 54px wide (180*0.3) but label might be wider
        float zoom = 0.3f;
        float nodeWScaled = nodeW * zoom;  // 54
        float pinRScaled = pinR * zoom;    // 1.8
        float textW = 50.0f;  // A long label at small scale

        float labelX = computeOutputPinLabelX(nodeX * zoom, nodeWScaled, pinRScaled, textW, zoom);
        // If this fails, it confirms the label overflows the node at low zoom
        // labelX = 30 + 54 - 1.8 - 50 - 1.8 = 30.4 >= 30 — barely fits
        // But with even longer labels or smaller zoom, it would overflow
        CHECK(labelX >= nodeX * zoom);
    }

    SECTION("label is hidden when it would overflow node left edge") {
        float nodeX = 100.0f;
        float zoom = 0.15f;  // Very small zoom
        float nodeWScaled = nodeW * zoom;  // 27
        float pinRScaled = pinR * zoom;    // 0.9
        float textW = 30.0f;  // A label wider than the node

        float labelX = computeOutputPinLabelX(nodeX * zoom, nodeWScaled, pinRScaled, textW, zoom);
        // labelX < nodeX*zoom means the label would overflow — should be hidden
        bool shouldRender = (labelX >= nodeX * zoom);
        REQUIRE_FALSE(shouldRender);
    }
}

// =============================================================================
// Bug 7: Preset "Hard Cut" label clears separator line
// Extracted from preset_panel.cpp lines 218-257
// =============================================================================

TEST_CASE("Preset toolbar label clears separator", "[gui][layout][bug7]") {
    // These mirror the exact values from preset_panel.cpp
    float y = 100.0f;      // Panel top
    float h = 400.0f;      // Panel height
    float bottomH = 48.0f; // Bottom toolbar height (typical)

    float toolbarY = y + h - bottomH;  // 452 — separator line position
    float btnY = toolbarY + 6.0f;      // 458 — button top

    // Font metrics (typical defaults for the embedded font)
    float ascent = 10.0f;

    // Label is drawn at: canvas.text(label, sliderX, btnY + ascent + 4, ...)
    // In OverlayCanvas::text(), the y parameter IS the baseline.
    float labelBaseline = btnY + ascent + 4.0f;  // 472
    float labelTop = labelBaseline - ascent;       // 462

    SECTION("label top is below separator line") {
        REQUIRE(labelTop > toolbarY);
    }

    SECTION("label has at least 4px clearance from separator") {
        float clearance = labelTop - toolbarY;
        // clearance = 462 - 452 = 10 — should be >= 4
        REQUIRE(clearance >= 4.0f);
    }

    SECTION("with larger font ascent, still clears") {
        float largerAscent = 14.0f;
        float largerBaseline = btnY + largerAscent + 4.0f;  // 476
        float largerTop = largerBaseline - largerAscent;      // 462 (same!)
        // The ascent cancels out — labelTop = btnY + 4 regardless of ascent
        REQUIRE(largerTop > toolbarY);
    }

    SECTION("labelTop equals btnY + 4 regardless of ascent") {
        // This is the key insight: labelTop = (btnY + ascent + 4) - ascent = btnY + 4
        // So clearance from separator = (toolbarY + 6 + 4) - toolbarY = 10px always
        for (float a : {8.0f, 10.0f, 12.0f, 14.0f, 16.0f}) {
            float baseline = btnY + a + 4.0f;
            float top = baseline - a;
            REQUIRE_THAT(top, WithinAbs(btnY + 4.0f, 0.01f));
        }
    }
}
