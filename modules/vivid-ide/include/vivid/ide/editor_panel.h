#pragma once

/**
 * @file editor_panel.h
 * @brief Native code editor panel (placeholder for Zep integration)
 *
 * Simple text editor for chain.cpp files with syntax highlighting.
 * Currently a minimal implementation - Zep integration planned.
 */

#include <vivid/gui/overlay_canvas.h>
#include <vivid/frame_input.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace vivid {

/**
 * @brief Code editor panel
 *
 * Provides a native code editor that:
 * - Opens/edits chain.cpp files
 * - Basic syntax highlighting for C++/WGSL
 * - Triggers hot-reload on save (Cmd+S)
 * - Shows compile error markers
 *
 * Phase 1: Basic text display and editing
 * Phase 2: Full Zep integration with vim mode
 */
class EditorPanel {
public:
    EditorPanel();
    ~EditorPanel();

    // Non-copyable
    EditorPanel(const EditorPanel&) = delete;
    EditorPanel& operator=(const EditorPanel&) = delete;

    /**
     * @brief Initialize editor
     * @return true on success
     */
    bool init();

    /**
     * @brief Open a file for editing
     * @param path File path to open
     * @return true if file was loaded
     */
    bool openFile(const std::string& path);

    /**
     * @brief Get currently open file path
     */
    const std::string& filePath() const;

    /**
     * @brief Save current file
     * @return true if saved successfully
     */
    bool save();

    /**
     * @brief Check if file has unsaved changes
     */
    bool isDirty() const;

    /**
     * @brief Render editor to canvas
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
     * @brief Handle character input
     * @param codepoint Unicode codepoint
     */
    void onChar(uint32_t codepoint);

    /**
     * @brief Handle key press
     * @param key GLFW key code
     * @param mods Modifier flags
     */
    void onKeyDown(int key, int mods);

    /**
     * @brief Set compile error to show in editor
     * @param line Line number (1-based), 0 to clear
     * @param message Error message
     */
    void setError(int line, const std::string& message);

    /**
     * @brief Clear any displayed error
     */
    void clearError();

    /**
     * @brief Set callback for when file is saved
     */
    void onSave(std::function<void(const std::string& path)> callback);

    /**
     * @brief Check if editor is focused
     */
    bool isFocused() const { return m_focused; }

    /**
     * @brief Set focus state
     */
    void setFocused(bool focused) { m_focused = focused; }

    /**
     * @brief Get cursor position
     */
    int cursorLine() const;
    int cursorColumn() const;

    /**
     * @brief Go to specific line
     */
    void gotoLine(int line);

    /**
     * @brief Get total line count
     */
    int lineCount() const;

    /**
     * @brief Get scroll offset (in lines)
     */
    int scrollOffset() const { return m_scrollOffset; }

    /**
     * @brief Scroll by delta lines
     */
    void scroll(int delta);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;

    bool m_focused = false;
    int m_scrollOffset = 0;
    std::function<void(const std::string&)> m_onSave;
};

} // namespace vivid
