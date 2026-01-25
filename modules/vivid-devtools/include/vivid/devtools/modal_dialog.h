#pragma once

/**
 * @file modal_dialog.h
 * @brief Base class for modal dialog overlays
 *
 * Modal dialogs display centered content over a darkened background.
 * They capture input and can be dismissed with Escape or clicking outside.
 */

#include <vivid/gui/overlay_canvas.h>
#include <vivid/gui/ui_style.h>
#include <vivid/frame_input.h>
#include <glm/glm.hpp>
#include <string>
#include <functional>

namespace vivid {

/**
 * @brief Base class for modal dialog overlays
 *
 * Features:
 * - Darkened background overlay
 * - Centered content panel
 * - Escape key to close
 * - Optional click-outside-to-close
 * - Title bar with close button
 *
 * Usage:
 * @code
 * class MyDialog : public ModalDialog {
 * public:
 *     MyDialog() : ModalDialog("My Dialog", 400, 300) {}
 * protected:
 *     void renderContent(OverlayCanvas& canvas, const glm::vec4& contentBounds,
 *                        const FrameInput& input, float scale) override {
 *         // Render dialog content here
 *     }
 * };
 * @endcode
 */
class ModalDialog {
public:
    /**
     * @brief Construct a modal dialog
     * @param title Dialog title
     * @param width Dialog width in logical pixels
     * @param height Dialog height in logical pixels
     */
    ModalDialog(const std::string& title, float width, float height);
    virtual ~ModalDialog();

    // Non-copyable
    ModalDialog(const ModalDialog&) = delete;
    ModalDialog& operator=(const ModalDialog&) = delete;

    // -------------------------------------------------------------------------
    /// @name Lifecycle
    /// @{

    /**
     * @brief Show the dialog
     */
    void show();

    /**
     * @brief Hide/close the dialog
     */
    void hide();

    /**
     * @brief Check if dialog is visible
     */
    bool isVisible() const { return m_visible; }

    /**
     * @brief Toggle visibility
     */
    void toggle() { m_visible ? hide() : show(); }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Rendering
    /// @{

    /**
     * @brief Render the dialog (call each frame when visible)
     * @param canvas Canvas for drawing
     * @param input Frame input
     * @param screenWidth Screen width in logical pixels
     * @param screenHeight Screen height in logical pixels
     * @param style UI style for colors
     *
     * All coordinates are in logical pixels. The canvas handles scaling internally.
     */
    void render(OverlayCanvas& canvas, const FrameInput& input,
                float screenWidth, float screenHeight,
                const UIStyle& style);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Input
    /// @{

    /**
     * @brief Handle key down event
     * @param key Key code
     * @param mods Modifier flags
     * @return true if input was consumed
     */
    bool onKeyDown(int key, int mods);

    /**
     * @brief Handle character input
     * @param codepoint Unicode codepoint
     */
    virtual void onChar(uint32_t codepoint) {}

    /**
     * @brief Check if dialog consumed input
     */
    bool consumedInput() const { return m_visible; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /**
     * @brief Enable/disable click-outside-to-close
     */
    void setClickOutsideToClose(bool enabled) { m_clickOutsideToClose = enabled; }
    bool clickOutsideToClose() const { return m_clickOutsideToClose; }

    /**
     * @brief Set close callback
     */
    void onClose(std::function<void()> callback) { m_onClose = std::move(callback); }

    /**
     * @brief Get dialog title
     */
    const std::string& title() const { return m_title; }

    /**
     * @brief Set dialog title
     */
    void setTitle(const std::string& title) { m_title = title; }

    /**
     * @brief Get/set dialog size
     */
    float width() const { return m_width; }
    float height() const { return m_height; }
    void setSize(float width, float height) { m_width = width; m_height = height; }

    /// @}

protected:
    /**
     * @brief Render dialog content (override in subclasses)
     * @param canvas Canvas for drawing
     * @param contentBounds Content area bounds (inside dialog chrome) in logical pixels
     * @param input Frame input
     * @param style UI style
     */
    virtual void renderContent(OverlayCanvas& canvas, const glm::vec4& contentBounds,
                               const FrameInput& input, const UIStyle& style) = 0;

    /**
     * @brief Handle content-specific input (override in subclasses)
     * @param input Frame input
     * @param contentBounds Content area bounds
     * @return true if input was consumed
     */
    virtual bool handleContentInput(const FrameInput& input, const glm::vec4& contentBounds) {
        return false;
    }

    std::string m_title;
    float m_width;
    float m_height;

private:
    bool m_visible = false;
    bool m_clickOutsideToClose = true;
    std::function<void()> m_onClose;

    // Input tracking
    bool m_lastMouseDown = false;
    glm::vec4 m_dialogBounds = {0, 0, 0, 0};  // Calculated during render

    static constexpr float kTitleBarHeight = 32.0f;
    static constexpr float kCornerRadius = 8.0f;
};

} // namespace vivid
