// Chain Visualizer - ImGui-based visualization of operator chains
// Displays a node graph view of all registered operators
#pragma once

#include "vivid/context.h"

namespace vivid {

/// Chain visualization overlay using ImGui
/// Shows the operator graph, thumbnails, and parameters (read-only)
class ChainVisualizer {
public:
    ChainVisualizer() = default;
    ~ChainVisualizer();

    /// Initialize the visualizer (creates ImGui context if needed)
    void init(Context& ctx);

    /// Begin a new visualization frame
    void beginFrame(Context& ctx);

    /// Render the visualization overlay
    void render(Context& ctx);

    /// Shutdown and cleanup
    void shutdown();

    /// Check if initialized
    bool isInitialized() const { return initialized_; }

    /// Toggle visibility
    void setVisible(bool visible) { visible_ = visible; }
    bool isVisible() const { return visible_; }

    /// Toggle the window (for keyboard shortcut)
    void toggleVisible() { visible_ = !visible_; }

private:
    void renderOperatorNode(Context& ctx, const OperatorInfo& info, int index);
    void renderConnections(Context& ctx);

    bool initialized_ = false;
    bool visible_ = true;

    // Window state
    float windowWidth_ = 400.0f;
    float nodeSpacing_ = 80.0f;
    int thumbnailSize_ = 180;
};

} // namespace vivid
