#pragma once

/**
 * @file gui_test_helpers.h
 * @brief Test helpers for GUI unit tests
 *
 * Provides TestPanel (a minimal Panel subclass) and makeInput() factory
 * for testing PanelManager without GPU context.
 */

#include <vivid/gui/panel.h>
#include <vivid/gui/input_state.h>

namespace vivid {
namespace test {

/**
 * @brief Minimal Panel subclass for testing PanelManager logic
 *
 * No GPU resources needed — init/render/shutdown are no-ops.
 * isContentInteracting() returns a settable bool for testing input routing.
 */
class TestPanel : public Panel {
public:
    TestPanel(const std::string& id, PanelRole role,
              const glm::vec4& bounds, bool visible = true) {
        m_config.id = id;
        m_config.title = id;
        m_config.bounds = bounds;
        m_config.role = role;
        m_config.visible = visible;
        m_config.resizable = (role == PanelRole::Floating);
        m_config.draggable = (role == PanelRole::Floating);
        m_config.minWidth = 100.0f;
        m_config.minHeight = 80.0f;

        m_display.showTitleBar = (role == PanelRole::Floating);
    }

    bool init(Context&, WGPUTextureFormat) override { return true; }
    void shutdown() override {}
    void render(OverlayCanvas&, const glm::vec4&,
                const gui::InputState&, const UIStyle&) override {}

    bool isContentInteracting() const override { return m_contentInteracting; }

    bool m_contentInteracting = false;
};

/**
 * @brief Create an InputState with sensible defaults (1920x1080, dt=1/60)
 */
inline gui::InputState makeInput(glm::vec2 mousePos = {0, 0}) {
    gui::InputState input;
    input.width = 1920;
    input.height = 1080;
    input.contentScale = 1.0f;
    input.dt = 1.0f / 60.0f;
    input.mousePos = mousePos;
    return input;
}

/**
 * @brief Create an InputState with a left-click at the given position
 */
inline gui::InputState makeClick(glm::vec2 mousePos) {
    auto input = makeInput(mousePos);
    input.mouseClicked[0] = true;
    input.mouseDown[0] = true;
    return input;
}

/**
 * @brief Create an InputState with scroll at the given position
 */
inline gui::InputState makeScroll(glm::vec2 mousePos, glm::vec2 scroll) {
    auto input = makeInput(mousePos);
    input.scroll = scroll;
    return input;
}

} // namespace test
} // namespace vivid
