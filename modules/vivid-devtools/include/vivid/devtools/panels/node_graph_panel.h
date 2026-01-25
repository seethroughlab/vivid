#pragma once

/**
 * @file node_graph_panel.h
 * @brief Node graph panel for operator visualization
 *
 * Wraps the NodeGraph class and provides panel chrome.
 */

#include <vivid/devtools/panel.h>
#include <memory>
#include <string>
#include <functional>

namespace vivid {

class NodeGraph;

/**
 * @brief Node graph panel for visualizing operator chains
 */
class NodeGraphPanel : public Panel {
public:
    NodeGraphPanel();
    ~NodeGraphPanel() override;

    // Panel interface
    bool init(Context& ctx, WGPUTextureFormat surfaceFormat) override;
    void shutdown() override;
    void render(OverlayCanvas& canvas, const glm::vec4& bounds,
               const FrameInput& input, float scale, const UIStyle& style) override;
    bool handleInput(const FrameInput& input) override;

    // Node graph specific
    void selectNode(const std::string& name);
    void setFocusedNode(const std::string& name);
    void clearFocusedNode();

    // Callbacks
    using NodeSelectCallback = std::function<void(const std::string&)>;
    using NodeDoubleClickCallback = std::function<void(const std::string&)>;

    void onNodeSelect(NodeSelectCallback callback);
    void onNodeDoubleClick(NodeDoubleClickCallback callback);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace vivid
