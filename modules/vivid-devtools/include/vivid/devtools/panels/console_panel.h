#pragma once

/**
 * @file console_panel.h
 * @brief Console panel for hot-reload errors and messages
 *
 * Provides selectable, scrollable console output for compile errors,
 * warnings, and debug messages. Replaces bitmap font error display.
 */

#include <vivid/gui/panel.h>
#include <vivid/hot_reload.h>
#include <memory>
#include <string>
#include <vector>
#include <functional>

struct GLFWwindow;

namespace vivid {

/**
 * @brief Message type for console entries
 */
enum class ConsoleMessageType {
    Info,           ///< Informational message
    Warning,        ///< Warning message
    Error,          ///< Error message
    Debug,          ///< Debug message
    CompileError    ///< Compile error (with file/line info)
};

/**
 * @brief Console panel for displaying errors and messages
 *
 * Features:
 * - Color-coded message types (info, warning, error, compile error)
 * - Scrollable message history (max 1000 messages)
 * - Auto-scroll to bottom on new messages (toggleable)
 * - Text selection with drag-select
 * - Clipboard support (Cmd+C / Ctrl+C)
 * - Timestamps for each message
 * - File:line display for compile errors
 */
class ConsolePanel : public Panel {
public:
    ConsolePanel();
    ~ConsolePanel() override;

    // Panel interface
    bool init(Context& ctx, WGPUTextureFormat surfaceFormat) override;
    void shutdown() override;
    void update() override;
    void render(OverlayCanvas& canvas, const glm::vec4& bounds,
               const gui::InputState& input, const UIStyle& style) override;
    bool handleInput(const gui::InputState& input) override;
    void onChar(uint32_t codepoint) override;
    void onKeyDown(int key, int mods) override;

    // Console-specific API

    /**
     * @brief Set compile errors from hot-reload
     * @param errors Structured compile errors
     */
    void setCompileErrors(const std::vector<CompileError>& errors);

    /**
     * @brief Clear all compile errors
     */
    void clearCompileErrors();

    /**
     * @brief Add a console message
     * @param type Message type
     * @param message Message text
     */
    void addMessage(ConsoleMessageType type, const std::string& message);

    /**
     * @brief Add a message with file/line info
     * @param type Message type
     * @param message Message text
     * @param file Source file path
     * @param line Line number
     */
    void addMessage(ConsoleMessageType type, const std::string& message,
                    const std::string& file, int line);

    /**
     * @brief Clear all messages
     */
    void clear();

    /**
     * @brief Get message count
     */
    size_t messageCount() const;

    /**
     * @brief Check if there are any errors displayed
     */
    bool hasErrors() const { return m_hasErrors; }

    /**
     * @brief Set auto-scroll behavior
     * @param enabled Whether to auto-scroll to new messages
     */
    void setAutoScroll(bool enabled) { m_autoScroll = enabled; }

    /**
     * @brief Get auto-scroll state
     */
    bool autoScrollEnabled() const { return m_autoScroll; }

    /**
     * @brief Set clipboard callbacks (for copy support)
     */
    void setClipboardCallbacks(
        std::function<std::string()> getClipboard,
        std::function<void(const std::string&)> setClipboard);

    /**
     * @brief Copy selected text to clipboard
     */
    void copySelection();

    /**
     * @brief Select all text
     */
    void selectAll();

    /**
     * @brief Clear selection
     */
    void clearSelection();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    bool m_autoScroll = true;
    bool m_hasErrors = false;
};

} // namespace vivid
