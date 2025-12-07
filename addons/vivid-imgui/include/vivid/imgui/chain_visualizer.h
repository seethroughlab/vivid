#pragma once

// Vivid Chain Visualizer
// ImNodes-based node editor for visualizing operator chains

#include <vivid/imgui/imgui_integration.h>
#include <vivid/context.h>
#include <vivid/operator.h>
#include <webgpu/webgpu.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace vivid::imgui {

class ChainVisualizer {
public:
    ChainVisualizer() = default;
    ~ChainVisualizer();

    // Initialize ImNodes context
    void init();

    // Cleanup
    void shutdown();

    // Render the chain visualizer
    // Call between imgui::beginFrame() and imgui::render()
    void render(const FrameInput& input, vivid::Context& ctx);

private:
    // Build graph layout from registered operators
    void buildLayout(const std::vector<vivid::OperatorInfo>& operators);

    // Attribute ID helpers
    int outputAttrId(int nodeId) { return nodeId * 100; }
    int inputAttrId(int nodeId, int inputIndex) { return nodeId * 100 + inputIndex + 1; }

    bool m_initialized = false;
    bool m_layoutBuilt = false;

    // Map operator pointers to node IDs
    std::unordered_map<vivid::Operator*, int> m_opToNodeId;

    // Node positions (indexed by node ID)
    std::unordered_map<int, bool> m_nodePositioned;
};

} // namespace vivid::imgui
