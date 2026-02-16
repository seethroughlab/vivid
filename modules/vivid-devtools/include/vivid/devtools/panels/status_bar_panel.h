#pragma once

/**
 * @file status_bar_panel.h
 * @brief Status bar panel (top edge)
 *
 * Shows:
 * - Grid opacity slider
 * - FPS, frame time, resolution, memory
 * - Pending changes indicator
 * - Record/Snapshot buttons
 */

#include <vivid/gui/panel.h>
#include <memory>
#include <string>
#include <functional>

namespace vivid {

class VideoExporter;
enum class ExportCodec;

/**
 * @brief Status bar panel fixed to top of screen
 */
class StatusBarPanel : public Panel {
public:
    StatusBarPanel();
    ~StatusBarPanel() override;

    // Panel interface
    bool init(Context& ctx, WGPUTextureFormat surfaceFormat) override;
    void shutdown() override;
    void render(OverlayCanvas& canvas, const glm::vec4& bounds,
               const gui::InputState& input, const UIStyle& style) override;
    bool handleInput(const gui::InputState& input) override;

    // Status bar specific
    void setPendingChangeCount(size_t count);
    void setMcpWarning(const std::string& warning);
    void setVideoExporter(VideoExporter* exporter);

    // Grid opacity (synced with DevTools background grid)
    void setGridOpacity(float opacity);
    float gridOpacity() const;

    // Callbacks
    using SnapshotCallback = std::function<void()>;
    using RecordCallback = std::function<void(bool start, ExportCodec codec)>;
    using GridOpacityCallback = std::function<void(float opacity)>;

    void onSnapshot(SnapshotCallback callback);
    void onRecord(RecordCallback callback);
    void onGridOpacityChange(GridOpacityCallback callback);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace vivid
