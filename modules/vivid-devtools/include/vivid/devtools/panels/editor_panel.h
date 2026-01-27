#pragma once

/**
 * @file editor_panel.h
 * @brief Code editor panel with syntax highlighting and tabbed editing
 *
 * Provides multi-file editing with:
 * - Tabbed interface (Cmd+W close, Cmd+Tab switch)
 * - Syntax highlighting for C++ and WGSL
 * - Compile error markers
 * - Line numbers
 * - Selection and clipboard
 */

#include <vivid/devtools/panel.h>
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace vivid {

class FileBuffer;

/**
 * @brief Code editor panel with multi-file tabs
 */
class EditorPanel : public Panel {
public:
    EditorPanel();
    ~EditorPanel() override;

    // Panel interface
    bool init(Context& ctx, WGPUTextureFormat surfaceFormat) override;
    void shutdown() override;
    void render(OverlayCanvas& canvas, const glm::vec4& bounds,
               const FrameInput& input, const UIStyle& style) override;
    bool handleInput(const FrameInput& input) override;
    void onChar(uint32_t codepoint) override;
    void onKeyDown(int key, int mods) override;

    // -------------------------------------------------------------------------
    /// @name File/Tab Management
    /// @{

    /**
     * @brief Open a file in a new tab (or switch to existing tab)
     * @param path File path to open
     * @return true on success
     */
    bool openFile(const std::string& path);

    /**
     * @brief Close a tab
     * @param index Tab index to close (-1 for current tab)
     * @param force Close even if dirty (skip unsaved warning)
     * @return true if closed, false if user cancelled
     */
    bool closeFile(int index = -1, bool force = false);

    /**
     * @brief Close all tabs
     * @param force Close even if dirty
     * @return true if all closed, false if user cancelled
     */
    bool closeAllFiles(bool force = false);

    /**
     * @brief Save the current file
     * @return true on success
     */
    bool saveFile();

    /**
     * @brief Save a specific file
     * @param index Tab index (-1 for current)
     * @return true on success
     */
    bool saveFile(int index);

    /**
     * @brief Get number of open tabs
     */
    int tabCount() const;

    /**
     * @brief Get active tab index
     */
    int activeTab() const;

    /**
     * @brief Set active tab
     * @param index Tab index to activate
     */
    void setActiveTab(int index);

    /**
     * @brief Switch to next tab (wraps around)
     */
    void nextTab();

    /**
     * @brief Switch to previous tab (wraps around)
     */
    void prevTab();

    /**
     * @brief Get file path for a tab
     */
    std::string tabPath(int index) const;

    /**
     * @brief Check if any tab has unsaved changes
     */
    bool hasUnsavedChanges() const;

    /**
     * @brief Get list of open file paths (for session persistence)
     */
    std::vector<std::string> openFiles() const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Error Markers
    /// @{

    /**
     * @brief Set error marker on current file
     */
    void setError(int line, const std::string& message);

    /**
     * @brief Clear error marker on current file
     */
    void clearError();

    /**
     * @brief Set error marker on a specific file
     */
    void setError(const std::string& path, int line, const std::string& message);

    /**
     * @brief Clear all error markers
     */
    void clearAllErrors();

    /// @}
    // -------------------------------------------------------------------------
    /// @name Callbacks
    /// @{

    /**
     * @brief Clipboard callbacks
     */
    using ClipboardGetCallback = std::function<std::string()>;
    using ClipboardSetCallback = std::function<void(const std::string&)>;
    void setClipboardCallbacks(ClipboardGetCallback get, ClipboardSetCallback set);

    /**
     * @brief Callback when a file is saved
     */
    using FileSaveCallback = std::function<void(const std::string& path)>;
    void onFileSave(FileSaveCallback callback);

    /**
     * @brief Callback when active tab changes
     */
    using TabChangeCallback = std::function<void(const std::string& path)>;
    void onTabChange(TabChangeCallback callback);

    /// @}

    // Mouse handling
    void onMouseClick(float x, float y, const glm::vec4& contentBounds,
                      float lineHeight, float charWidth);
    void onMouseDrag(float x, float y);
    void onMouseUp();
    bool isEditorDragging() const { return m_editorDragging; }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    bool m_editorDragging = false;

    // Tab bar rendering helpers
    void renderTabBar(OverlayCanvas& canvas, float x, float y, float w,
                      const FrameInput& input, const UIStyle& style);
    bool handleTabBarInput(const FrameInput& input, float x, float y, float w, float tabHeight);
};

} // namespace vivid
