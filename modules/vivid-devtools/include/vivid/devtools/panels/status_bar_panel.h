#pragma once

/**
 * @file status_bar_panel.h
 * @brief Status bar panel (docked at top)
 *
 * Shows:
 * - Panel toggle buttons with three-state rendering (docked/floating/hidden)
 * - Record button
 * - Snapshot button
 * - Pending changes indicator
 * - Memory usage
 * - FPS
 */

#include <vivid/gui/panel.h>
#include <vivid/gui/dock_zone.h>
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

    // Grid opacity (synced with DevTools background grid)
    void setGridOpacity(float opacity);
    float gridOpacity() const;

    // Panel visibility and dock mode (synced with DevTools)
    void setPanelVisibility(const std::string& panelId, bool visible);
    void setPanelDockMode(const std::string& panelId, DockMode mode);

    // Callbacks
    using SnapshotCallback = std::function<void()>;
    using RecordCallback = std::function<void(bool start)>;
    using GridOpacityCallback = std::function<void(float opacity)>;
    using PanelToggleCallback = std::function<void(const std::string& panelId)>;
    using PanelDragCallback = std::function<void(const std::string& panelId, const glm::vec2& pos)>;
    using PanelDockCallback = std::function<void(const std::string& panelId, DockPosition position)>;

    void onSnapshot(SnapshotCallback callback);
    void onRecord(RecordCallback callback);
    void onGridOpacityChange(GridOpacityCallback callback);
    void onPanelToggle(PanelToggleCallback callback);
    void onPanelDrag(PanelDragCallback callback);      // Drag from button to create floating
    void onPanelDock(PanelDockCallback callback);      // Context menu dock action

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace vivid
