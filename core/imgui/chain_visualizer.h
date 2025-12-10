#pragma once

// Vivid Chain Visualizer
// ImNodes-based node editor for visualizing operator chains

#include "imgui_integration.h"
#include <vivid/context.h>
#include <vivid/operator.h>
#include <vivid/video_exporter.h>
#include <vivid/render3d/render3d.h>
#include <vivid/render3d/scene_composer.h>
#include <webgpu/webgpu.h>
#include <string>
#include <unordered_map>
#include <map>
#include <vector>
#include <memory>
#include <array>

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

    // Load sidecar file and apply parameter overrides to operators
    // Call after chain is initialized (operators are registered)
    void loadAndApplySidecar(vivid::Context& ctx);

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

    // Estimate node height based on content (params, inputs, thumbnail type)
    float estimateNodeHeight(const vivid::OperatorInfo& info) const;

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

    // Solo mode state
    vivid::Operator* m_soloOperator = nullptr;
    bool m_inSoloMode = false;
    std::string m_soloOperatorName;  // Cached for display

    // Full-viewport geometry renderer for solo mode
    std::unique_ptr<render3d::Render3D> m_soloGeometryRenderer;
    float m_soloRotationAngle = 0.0f;

    // Solo mode helpers
    void enterSoloMode(vivid::Operator* op, const std::string& name);
    void exitSoloMode();
    void renderSoloOverlay(const FrameInput& input, vivid::Context& ctx);

    // Parameter sidecar file
    // Key: "operator_name.param_name", Value: up to 4 floats
    std::map<std::string, std::array<float, 4>> m_paramOverrides;
    std::string m_sidecarPath;
    bool m_sidecarDirty = false;

    // Sidecar file helpers
    void loadSidecar(const std::string& chainPath);
    void saveSidecar();
    void applyOverrides(const std::vector<vivid::OperatorInfo>& operators);
    std::string makeParamKey(const std::string& opName, const std::string& paramName);

    // Video recording
    VideoExporter m_exporter;
    void startRecording(ExportCodec codec, vivid::Context& ctx);
    void stopRecording(vivid::Context& ctx);

public:
    // Access to exporter for main.cpp to call captureFrame
    VideoExporter& exporter() { return m_exporter; }
};

} // namespace vivid::imgui
