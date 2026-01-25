#pragma once

/**
 * @file editor_panel.h
 * @brief Code editor panel with syntax highlighting
 *
 * Provides chain.cpp editing with:
 * - Syntax highlighting for C++
 * - Compile error markers
 * - Line numbers
 * - Selection and clipboard
 */

#include <vivid/devtools/panel.h>
#include <memory>
#include <string>
#include <functional>

namespace vivid {

/**
 * @brief Code editor panel
 */
class EditorPanel : public Panel {
public:
    EditorPanel();
    ~EditorPanel() override;

    // Panel interface
    bool init(Context& ctx, WGPUTextureFormat surfaceFormat) override;
    void shutdown() override;
    void render(OverlayCanvas& canvas, const glm::vec4& bounds,
               const FrameInput& input, float scale, const UIStyle& style) override;
    bool handleInput(const FrameInput& input) override;
    void onChar(uint32_t codepoint) override;
    void onKeyDown(int key, int mods) override;

    // Editor-specific
    bool openFile(const std::string& path);
    bool saveFile();
    void setError(int line, const std::string& message);
    void clearError();

    // Clipboard
    using ClipboardGetCallback = std::function<std::string()>;
    using ClipboardSetCallback = std::function<void(const std::string&)>;
    void setClipboardCallbacks(ClipboardGetCallback get, ClipboardSetCallback set);

    // Mouse handling
    void onMouseClick(float x, float y, const glm::vec4& contentBounds,
                      float lineHeight, float charWidth);
    void onMouseDrag(float x, float y);
    void onMouseUp();
    bool isDragging() const { return m_dragging; }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    bool m_dragging = false;
};

} // namespace vivid
