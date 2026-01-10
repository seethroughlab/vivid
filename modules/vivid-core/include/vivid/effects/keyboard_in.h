#pragma once

/**
 * @file keyboard_in.h
 * @brief Keyboard input event operator
 *
 * Exposes keyboard input to user chains as discrete events.
 */

#include <vivid/operator.h>
#include <vivid/param.h>
#include <vivid/param_registry.h>
#include <vivid/event.h>
#include <vivid/context.h>
#include <vector>
#include <array>

namespace vivid {

/**
 * @brief Keyboard input operator
 *
 * Makes keyboard events available in user chains. Events are collected
 * each frame and can be polled via events() or convenience accessors.
 *
 * @par Example
 * @code
 * auto& keys = chain.add<KeyboardIn>("keys");
 *
 * // In update():
 * if (keys.keyPressed(GLFW_KEY_SPACE)) {
 *     flash.trigger();
 * }
 *
 * // Or iterate all events:
 * for (const auto& e : keys.events()) {
 *     if (e.type == EventType::KeyPress) {
 *         std::cout << "Key pressed: " << e.code << "\n";
 *     }
 * }
 * @endcode
 *
 * @see MouseIn, WindowEvents, MidiIn
 */
class KeyboardIn : public Operator, public ParamRegistry {
public:
    KeyboardIn();
    ~KeyboardIn() override = default;

    // -------------------------------------------------------------------------
    /// @name Event Access
    /// @{

    /// @brief Get all keyboard events this frame
    [[nodiscard]] const std::vector<Event>& events() const { return m_frameEvents; }

    /// @brief Check if any key was pressed this frame
    [[nodiscard]] bool anyKeyPressed() const { return m_anyKeyPressed; }

    /// @brief Check if a specific key was pressed this frame
    [[nodiscard]] bool keyPressed(int keyCode) const;

    /// @brief Check if a specific key is currently held
    [[nodiscard]] bool keyHeld(int keyCode) const;

    /// @brief Check if a specific key was released this frame
    [[nodiscard]] bool keyReleased(int keyCode) const;

    /// @brief Get the last key that was pressed (key code)
    [[nodiscard]] int lastKeyPressed() const { return m_lastKeyPressed; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Modifier Keys
    /// @{

    /// @brief Check if shift is held
    [[nodiscard]] bool shiftHeld() const { return m_shiftHeld; }

    /// @brief Check if control is held
    [[nodiscard]] bool ctrlHeld() const { return m_ctrlHeld; }

    /// @brief Check if alt/option is held
    [[nodiscard]] bool altHeld() const { return m_altHeld; }

    /// @brief Check if super/command is held
    [[nodiscard]] bool superHeld() const { return m_superHeld; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    std::string name() const override { return "KeyboardIn"; }
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
    static constexpr int MAX_KEYS = 512;

    std::vector<Event> m_frameEvents;
    std::array<bool, MAX_KEYS> m_keyHeld{};
    std::array<bool, MAX_KEYS> m_keyPressedThisFrame{};
    std::array<bool, MAX_KEYS> m_keyReleasedThisFrame{};

    bool m_anyKeyPressed = false;
    int m_lastKeyPressed = 0;

    bool m_shiftHeld = false;
    bool m_ctrlHeld = false;
    bool m_altHeld = false;
    bool m_superHeld = false;

    void clearFrameState();
};

} // namespace vivid
