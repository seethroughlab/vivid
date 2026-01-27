#pragma once

/**
 * @file file_browser_panel.h
 * @brief File browser panel for project navigation
 *
 * Tree view of project files with:
 * - Expand/collapse folders (click arrow)
 * - Open files in editor (double-click)
 * - Filter by file type (.cpp, .h, .wgsl, .json, .md)
 * - Keyboard shortcut: Cmd+5
 */

#include <vivid/devtools/panel.h>
#include <memory>
#include <string>
#include <functional>
#include <set>

namespace vivid {

/**
 * @brief File browser panel for project navigation
 */
class FileBrowserPanel : public Panel {
public:
    FileBrowserPanel();
    ~FileBrowserPanel() override;

    // Panel interface
    bool init(Context& ctx, WGPUTextureFormat surfaceFormat) override;
    void shutdown() override;
    void render(OverlayCanvas& canvas, const glm::vec4& bounds,
               const FrameInput& input, const UIStyle& style) override;
    bool handleInput(const FrameInput& input) override;
    void onKeyDown(int key, int mods) override;

    /**
     * @brief Set root directory for browsing
     * @param path Directory path
     */
    void setRootDirectory(const std::string& path);

    /**
     * @brief Get the root directory
     */
    const std::string& rootDirectory() const;

    /**
     * @brief Refresh the file tree
     */
    void refresh();

    /**
     * @brief Set callback for when a file is selected (double-click)
     */
    using FileSelectedCallback = std::function<void(const std::string& path)>;
    void onFileSelected(FileSelectedCallback callback);

    /**
     * @brief Get list of expanded folder paths (for session persistence)
     */
    std::set<std::string> expandedFolders() const;

    /**
     * @brief Set expanded folders (for session restore)
     */
    void setExpandedFolders(const std::set<std::string>& folders);

    /**
     * @brief Select a file by path (highlights it in the tree)
     */
    void selectFile(const std::string& path);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace vivid
