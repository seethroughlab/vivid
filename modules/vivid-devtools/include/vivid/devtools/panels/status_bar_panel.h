#pragma once

/**
 * @file status_bar_panel.h
 * @brief Status bar panel (docked at top)
 *
 * Shows:
 * - Record button
 * - Snapshot button
 * - Pending changes indicator
 * - Memory usage
 * - FPS
 */

#include <vivid/devtools/panel.h>
#include <memory>
#include <string>
#include <functional>

namespace vivid {

class VideoExporter;

/**
 * @brief Status bar panel docked at top of screen
 */
class StatusBarPanel : public Panel {
public:
    StatusBarPanel();
    ~StatusBarPanel() override;

    // Panel interface
    bool init(Context& ctx, WGPUTextureFormat surfaceFormat) override;
    void shutdown() override;
    void render(OverlayCanvas& canvas, const glm::vec4& bounds,
               const FrameInput& input, const UIStyle& style) override;
    bool handleInput(const FrameInput& input) override;

    // Status bar specific
    void setPendingChangeCount(size_t count);
    void setMcpWarning(const std::string& warning);
    void setVideoExporter(VideoExporter* exporter);

    // Grid toggle (synced with DevTools background grid)
    void setGridVisible(bool visible);
    bool isGridVisible() const;

    // Panel visibility (synced with DevTools)
    void setPanelVisibility(const std::string& panelId, bool visible);

    // Callbacks
    using SnapshotCallback = std::function<void()>;
    using RecordCallback = std::function<void(bool start)>;
    using GridToggleCallback = std::function<void(bool visible)>;
    using PanelToggleCallback = std::function<void(const std::string& panelId)>;

    void onSnapshot(SnapshotCallback callback);
    void onRecord(RecordCallback callback);
    void onGridToggle(GridToggleCallback callback);
    void onPanelToggle(PanelToggleCallback callback);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace vivid
