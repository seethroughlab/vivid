#pragma once

/**
 * @file terminal_panel.h
 * @brief Native terminal emulator panel using libtmt
 *
 * Renders a VT100-compatible terminal via OverlayCanvas, with PTY
 * integration for running shell commands and Claude Code.
 */

#include <vivid/gui/overlay_canvas.h>
#include <vivid/frame_input.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <functional>

namespace vivid {

class PTY;  // Forward declaration

/**
 * @brief Terminal panel using libtmt for VT100 emulation
 *
 * Provides a native terminal that:
 * - Spawns a shell via PTY
 * - Emulates VT100/ANSI escape sequences via libtmt
 * - Renders character grid via OverlayCanvas text primitives
 * - Handles keyboard input routing
 *
 * Usage:
 * @code
 * TerminalPanel terminal;
 * terminal.init();
 * terminal.spawn("/bin/zsh", "/path/to/project");
 *
 * // Each frame:
 * terminal.update();  // Process PTY output
 * terminal.render(canvas, bounds);
 * terminal.handleInput(input);
 * @endcode
 */
class TerminalPanel {
public:
    TerminalPanel();
    ~TerminalPanel();

    // Non-copyable
    TerminalPanel(const TerminalPanel&) = delete;
    TerminalPanel& operator=(const TerminalPanel&) = delete;

    /**
     * @brief Initialize terminal emulator
     * @param cols Number of columns (default 80)
     * @param rows Number of rows (default 24)
     * @return true on success
     */
    bool init(int cols = 80, int rows = 24);

    /**
     * @brief Spawn a shell process
     * @param shell Shell command (empty = user's default shell)
     * @param workingDir Working directory for the shell
     * @return true if PTY started successfully
     */
    bool spawn(const std::string& shell = "", const std::string& workingDir = "");

    /**
     * @brief Check if terminal is running
     */
    bool isRunning() const;

    /**
     * @brief Update terminal state (read PTY output, process escape sequences)
     * Call this each frame.
     */
    void update();

    /**
     * @brief Render terminal to canvas
     * @param canvas OverlayCanvas to render to
     * @param bounds Panel bounds (x, y, width, height)
     * @param fontIndex Font to use (typically 2 = monospace)
     */
    void render(OverlayCanvas& canvas, const glm::vec4& bounds, int fontIndex = 2);

    /**
     * @brief Handle keyboard input
     * @param input Frame input
     * @return true if input was consumed
     */
    bool handleInput(const FrameInput& input);

    /**
     * @brief Handle character input (for text entry)
     * @param codepoint Unicode codepoint
     */
    void onChar(uint32_t codepoint);

    /**
     * @brief Handle key press (for special keys)
     * @param key GLFW key code
     * @param mods Modifier flags
     */
    void onKeyDown(int key, int mods);

    /**
     * @brief Resize terminal
     * @param cols New column count
     * @param rows New row count
     */
    void resize(int cols, int rows);

    /**
     * @brief Get current terminal size
     */
    int cols() const;
    int rows() const;

    /**
     * @brief Write data directly to PTY (e.g., from paste)
     */
    void write(const std::string& data);

    /**
     * @brief Stop the terminal process
     */
    void stop();

    /**
     * @brief Set callback for when terminal exits
     */
    void onExit(std::function<void(int exitCode)> callback);

    /**
     * @brief Check if terminal is focused
     */
    bool isFocused() const { return m_focused; }

    /**
     * @brief Set focus state
     */
    void setFocused(bool focused) { m_focused = focused; }

    /**
     * @brief Get scroll position (0 = bottom, positive = scrolled up)
     */
    int scrollOffset() const { return m_scrollOffset; }

    /**
     * @brief Scroll by delta lines
     */
    void scroll(int delta);

    /**
     * @brief Scroll to bottom
     */
    void scrollToBottom();

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;

    bool m_focused = false;
    int m_scrollOffset = 0;
    std::function<void(int)> m_onExit;
};

} // namespace vivid
