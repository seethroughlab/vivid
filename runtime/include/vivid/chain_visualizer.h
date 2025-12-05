// Chain Visualizer - ImGui-based visualization of operator chains
// Displays a node graph view of all registered operators
#pragma once

#include "vivid/context.h"
#include <string>

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

    /// Set compile error to display (shows error overlay)
    void setError(const std::string& errorMessage);

    /// Clear compile error (hides error overlay)
    void clearError();

    /// Check if there's an error being displayed
    bool hasError() const { return hasError_; }

private:
    void renderErrorOverlay();
    void renderNodeGraph(Context& ctx);
    void buildGraphLayout(Context& ctx);

    bool initialized_ = false;
    bool visible_ = true;
    bool imnodesInitialized_ = false;

    // Node graph state
    int thumbnailSize_ = 120;
    bool needsLayout_ = true;
    size_t lastOperatorCount_ = 0;

    // Error display state
    bool hasError_ = false;
    std::string errorMessage_;
};

} // namespace vivid
