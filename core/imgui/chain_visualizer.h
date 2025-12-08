#pragma once

// Vivid Chain Visualizer
// ImNodes-based node editor for visualizing operator chains

#include "imgui_integration.h"
#include <vivid/context.h>
#include <vivid/operator.h>
#include <vivid/render3d/render3d.h>
#include <vivid/render3d/scene_composer.h>
#include <webgpu/webgpu.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

namespace vivid::imgui {

/// Preview state for a geometry operator node
struct GeometryPreview {
    std::unique_ptr<render3d::Render3D> renderer;
    render3d::Scene scene;
    render3d::Camera3D camera;
    render3d::Mesh* lastMesh = nullptr;  // Track changes
    float rotationAngle = 0.0f;          // For animation
};

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

    // Update geometry preview (handles init, rotation, rendering)
    void updateGeometryPreview(GeometryPreview& preview, render3d::Mesh* mesh,
                               vivid::Context& ctx, float dt);

    // Update scene preview for SceneComposer (renders full composed scene)
    void updateScenePreview(GeometryPreview& preview, render3d::SceneComposer* composer,
                            vivid::Context& ctx, float dt);

    // Attribute ID helpers
    int outputAttrId(int nodeId) { return nodeId * 100; }
    int inputAttrId(int nodeId, int inputIndex) { return nodeId * 100 + inputIndex + 1; }

    bool m_initialized = false;
    bool m_layoutBuilt = false;

    // Map operator pointers to node IDs
    std::unordered_map<vivid::Operator*, int> m_opToNodeId;

    // Node positions (indexed by node ID)
    std::unordered_map<int, bool> m_nodePositioned;

    // Geometry preview renderers (one per geometry node)
    std::unordered_map<vivid::Operator*, GeometryPreview> m_geometryPreviews;
};

} // namespace vivid::imgui
