#pragma once

/**
 * @file console_panel.h
 * @brief Read-only console/log overlay panel
 *
 * Scrolling HUD showing hot-reload status, compile errors, warnings,
 * and operator messages. Not an interactive terminal — a heads-up display
 * for the runtime.
 *
 * - Color-coded by severity (Info=white, Warn=yellow, Error=red, Debug=dim)
 * - Auto-scrolls to newest messages; scroll wheel to browse history
 * - 256-message ring buffer with bounded memory
 * - Thread-safe pushMessage() for Log callback
 * - Toggled with Cmd+2
 */

#include <vivid/gui/panel.h>
#include <vivid/log.h>
#include <memory>
#include <string>

namespace vivid {

/**
 * @brief Read-only console/log overlay panel
 *
 * Displays a scrolling log of runtime messages with color-coded severity.
 * Semi-transparent background so the visual output is still visible behind it.
 */
class ConsolePanel : public Panel {
public:
    ConsolePanel();
    ~ConsolePanel() override;

    // Panel interface
    bool init(Context& ctx, WGPUTextureFormat surfaceFormat) override;
    void shutdown() override;
    void render(OverlayCanvas& canvas, const glm::vec4& bounds,
               const gui::InputState& input, const UIStyle& style) override;
    bool handleInput(const gui::InputState& input) override;

    /**
     * @brief Push a log message (thread-safe)
     *
     * Called from Log callback — may be invoked from any thread
     * (including audio thread). Uses a mutex to protect the ring buffer.
     */
    void pushMessage(LogLevel level, const char* file, int line, const std::string& message);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace vivid
