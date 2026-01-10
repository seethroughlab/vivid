#pragma once

/**
 * @file mouse_in.h
 * @brief Mouse input event operator
 *
 * Exposes mouse input to user chains as discrete events.
 */

#include <vivid/operator.h>
#include <vivid/param.h>
#include <vivid/param_registry.h>
#include <vivid/event.h>
#include <vivid/context.h>
#include <glm/glm.hpp>
#include <vector>
#include <array>

namespace vivid {

/**
 * @brief Mouse input operator
 *
 * Makes mouse events available in user chains. Events are collected
 * each frame and can be polled via events() or convenience accessors.
 *
 * @par Example
 * @code
 * auto& mouse = chain.add<MouseIn>("mouse");
 *
 * // In update():
 * if (mouse.buttonPressed(0)) {  // Left button
 *     particles.spawn(mouse.position());
 * }
 *
 * // Or iterate all events:
 * for (const auto& e : mouse.events()) {
 *     if (e.type == EventType::MouseScroll) {
 *         zoom += e.value2;  // Scroll Y
 *     }
 * }
 * @endcode
 *
 * @see KeyboardIn, WindowEvents, MidiIn
 */
class MouseIn : public Operator, public ParamRegistry {
public:
    MouseIn();
    ~MouseIn() override = default;

    // -------------------------------------------------------------------------
    /// @name Event Access
    /// @{

    /// @brief Get all mouse events this frame
    [[nodiscard]] const std::vector<Event>& events() const { return m_frameEvents; }

    /// @brief Get current mouse position in pixels
    [[nodiscard]] glm::vec2 position() const { return m_position; }

    /// @brief Get normalized mouse position (0-1 in both axes)
    [[nodiscard]] glm::vec2 positionNorm() const { return m_positionNorm; }

    /// @brief Get mouse movement delta since last frame (pixels)
    [[nodiscard]] glm::vec2 delta() const { return m_delta; }

    /// @brief Get normalized mouse delta
    [[nodiscard]] glm::vec2 deltaNorm() const { return m_deltaNorm; }

    /// @brief Get scroll delta this frame
    [[nodiscard]] glm::vec2 scroll() const { return m_scroll; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Button State
    /// @{

    /// @brief Check if any button was pressed this frame
    [[nodiscard]] bool anyButtonPressed() const { return m_anyButtonPressed; }

    /// @brief Check if a button was pressed this frame (0=left, 1=right, 2=middle)
    [[nodiscard]] bool buttonPressed(int button) const;

    /// @brief Check if a button is currently held
    [[nodiscard]] bool buttonHeld(int button) const;

    /// @brief Check if a button was released this frame
    [[nodiscard]] bool buttonReleased(int button) const;

    /// @brief Convenience: left button pressed
    [[nodiscard]] bool leftPressed() const { return buttonPressed(0); }

    /// @brief Convenience: right button pressed
    [[nodiscard]] bool rightPressed() const { return buttonPressed(1); }

    /// @brief Convenience: middle button pressed
    [[nodiscard]] bool middlePressed() const { return buttonPressed(2); }

    /// @brief Convenience: left button held
    [[nodiscard]] bool leftHeld() const { return buttonHeld(0); }

    /// @brief Convenience: right button held
    [[nodiscard]] bool rightHeld() const { return buttonHeld(1); }

    /// @brief Convenience: middle button held
    [[nodiscard]] bool middleHeld() const { return buttonHeld(2); }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    std::string name() const override { return "MouseIn"; }
    OutputKind outputKind() const override { return OutputKind::Event; }

    std::vector<ParamDecl> params() override { return registeredParams(); }
    bool getParam(const std::string& name, float out[4]) override {
        return getRegisteredParam(name, out);
    }
    bool setParam(const std::string& name, const float value[4]) override {
        return setRegisteredParam(name, value);
    }

    /// @}

private:
    static constexpr int MAX_BUTTONS = 3;

    std::vector<Event> m_frameEvents;

    glm::vec2 m_position{0, 0};
    glm::vec2 m_positionNorm{0, 0};
    glm::vec2 m_lastPosition{0, 0};
    glm::vec2 m_delta{0, 0};
    glm::vec2 m_deltaNorm{0, 0};
    glm::vec2 m_scroll{0, 0};

    std::array<bool, MAX_BUTTONS> m_buttonHeld{};
    std::array<bool, MAX_BUTTONS> m_buttonPressedThisFrame{};
    std::array<bool, MAX_BUTTONS> m_buttonReleasedThisFrame{};

    bool m_anyButtonPressed = false;
    bool m_firstFrame = true;

    void clearFrameState();
};

} // namespace vivid
