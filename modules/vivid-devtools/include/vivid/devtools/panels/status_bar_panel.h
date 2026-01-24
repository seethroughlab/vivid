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
               const FrameInput& input, float scale) override;
    bool handleInput(const FrameInput& input) override;

    // Status bar specific
    void setPendingChangeCount(size_t count);
    void setMcpWarning(const std::string& warning);
    void setVideoExporter(VideoExporter* exporter);

    // Callbacks
    using SnapshotCallback = std::function<void()>;
    using RecordCallback = std::function<void(bool start)>;

    void onSnapshot(SnapshotCallback callback);
    void onRecord(RecordCallback callback);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace vivid
