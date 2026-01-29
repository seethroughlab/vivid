#pragma once

/**
 * @file performance_panel.h
 * @brief Performance monitoring panel with real-time graphs
 *
 * Displays time-series graphs of:
 * - FPS (frames per second)
 * - Frame time (milliseconds)
 * - Memory usage (MB)
 * - DSP load (if audio is active)
 */

#include <vivid/gui/panel.h>
#include <memory>

namespace vivid {

/**
 * @brief Performance monitoring panel with real-time graphs
 *
 * Shows historical performance metrics with interactive graphs.
 * Updates automatically each frame with new samples.
 */
class PerformancePanel : public Panel {
public:
    PerformancePanel();
    ~PerformancePanel() override;

    // Panel interface
    bool init(Context& ctx, WGPUTextureFormat surfaceFormat) override;
    void shutdown() override;
    void update() override;
    void render(OverlayCanvas& canvas, const glm::vec4& bounds,
               const gui::InputState& input, const UIStyle& style) override;
    bool handleInput(const gui::InputState& input) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace vivid
