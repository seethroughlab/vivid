#pragma once

/**
 * @file window_events.h
 * @brief Window event operator
 *
 * Exposes window events (resize, focus) to user chains.
 */

#include <vivid/operator.h>
#include <vivid/param.h>
#include <vivid/param_registry.h>
#include <vivid/event.h>
#include <vivid/context.h>
#include <vector>

namespace vivid {

/**
 * @brief Window event operator
 *
 * Makes window events (resize, focus changes) available in user chains.
 *
 * @par Example
 * @code
 * auto& window = chain.add<WindowEvents>("window");
 *
 * // In update():
 * if (window.resized()) {
 *     // Recreate resolution-dependent resources
 *     int w = window.width();
 *     int h = window.height();
 * }
 * @endcode
 *
 * @see KeyboardIn, MouseIn
 */
class WindowEvents : public Operator, public ParamRegistry {
public:
    WindowEvents() = default;
    ~WindowEvents() override = default;

    // -------------------------------------------------------------------------
    /// @name Event Access
    /// @{

    /// @brief Get all window events this frame
    [[nodiscard]] const std::vector<Event>& events() const { return m_frameEvents; }

    /// @brief Check if window was resized this frame
    [[nodiscard]] bool resized() const { return m_resized; }

    /// @brief Get current window width
    [[nodiscard]] int width() const { return m_width; }

    /// @brief Get current window height
    [[nodiscard]] int height() const { return m_height; }

    /// @brief Get window aspect ratio (width/height)
    [[nodiscard]] float aspect() const { return m_height > 0 ? static_cast<float>(m_width) / m_height : 1.0f; }

    /// @brief Check if window gained focus this frame
    [[nodiscard]] bool focused() const { return m_focused; }

    /// @brief Check if window lost focus this frame
    [[nodiscard]] bool unfocused() const { return m_unfocused; }

    /// @brief Check if window currently has focus
    [[nodiscard]] bool hasFocus() const { return m_hasFocus; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    std::string name() const override { return "WindowEvents"; }
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
    std::vector<Event> m_frameEvents;

    int m_width = 0;
    int m_height = 0;
    int m_lastWidth = 0;
    int m_lastHeight = 0;

    bool m_resized = false;
    bool m_focused = false;
    bool m_unfocused = false;
    bool m_hasFocus = true;
    bool m_lastFocus = true;

    void clearFrameState();
};

} // namespace vivid
