#pragma once

/**
 * @file terminal_panel.h
 * @brief Terminal panel with PTY support
 *
 * Provides shell access for Claude Code and build commands.
 * Uses libvterm for VT220/xterm terminal emulation.
 */

#include <vivid/devtools/panel.h>
#include <memory>
#include <string>
#include <functional>

namespace vivid {

/**
 * @brief Terminal panel with PTY and VT terminal emulation
 *
 * Features:
 * - Full VT220/xterm emulation via libvterm
 * - PTY support for interactive shells
 * - Scroll history
 * - ANSI color support
 */
class TerminalPanel : public Panel {
public:
    TerminalPanel();
    ~TerminalPanel() override;

    // Panel interface
    bool init(Context& ctx, WGPUTextureFormat surfaceFormat) override;
    void shutdown() override;
    void update() override;
    void render(OverlayCanvas& canvas, const glm::vec4& bounds,
               const FrameInput& input, const UIStyle& style) override;
    bool handleInput(const FrameInput& input) override;
    void onChar(uint32_t codepoint) override;
    void onKeyDown(int key, int mods) override;

    // Terminal-specific
    void spawn(const std::string& command, const std::string& workingDir);
    void stop();
    bool isRunning() const { return m_running; }

    void resize(int cols, int rows);
    int cols() const { return m_cols; }
    int rows() const { return m_rows; }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    int m_cols = 80;
    int m_rows = 24;
    bool m_running = false;
};

} // namespace vivid
