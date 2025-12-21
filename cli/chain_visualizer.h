#pragma once

// Vivid Chain Visualizer
// ImNodes-based node editor for visualizing operator chains

#include "imgui/imgui_integration.h"
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
    std::unique_ptr<render3d::CameraOperator> cameraOp;
    render3d::Scene scene;
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

    // Select a node from external source (e.g., VSCode extension)
    // Will highlight the node in the graph
    void selectNodeFromEditor(const std::string& operatorName);

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

    // Selection state for inspector panel
    int m_selectedNodeId = -1;
    vivid::Operator* m_selectedOp = nullptr;
    std::string m_selectedOpName;

    // Solo mode state
    vivid::Operator* m_soloOperator = nullptr;
    bool m_inSoloMode = false;
    std::string m_soloOperatorName;  // Cached for display

    // Full-viewport geometry renderer for solo mode
    std::unique_ptr<render3d::Render3D> m_soloGeometryRenderer;
    std::unique_ptr<render3d::CameraOperator> m_soloCameraOp;
    float m_soloRotationAngle = 0.0f;

    // Solo mode helpers (internal)
    void renderSoloOverlay(const FrameInput& input, vivid::Context& ctx);

    // Debug value panel (shows ctx.debug() values with sparkline graphs)
    void renderDebugPanel(vivid::Context& ctx);

    // Selection helpers (for editor sync)
    void updateSelection(const std::vector<vivid::OperatorInfo>& operators);
    void clearSelection();

    // Pending editor selection (set by selectNodeFromEditor, applied in render)
    std::string m_pendingEditorSelection;

    // Focused node mode (cursor is in operator code in editor)
    std::string m_focusedOperatorName;
    bool m_focusedModeActive = false;

    // Video recording
    VideoExporter m_exporter;
    void startRecording(ExportCodec codec, vivid::Context& ctx);
    void stopRecording(vivid::Context& ctx);

    // Snapshot
    bool m_snapshotRequested = false;
    void requestSnapshot() { m_snapshotRequested = true; }

public:
    // Access to exporter for main.cpp to call captureFrame
    VideoExporter& exporter() { return m_exporter; }

    // Save a single frame snapshot (call from main loop after rendering)
    void saveSnapshot(WGPUDevice device, WGPUQueue queue, WGPUTexture texture, vivid::Context& ctx);
    bool snapshotRequested() const { return m_snapshotRequested; }

    // Solo mode control (for EditorBridge integration)
    void enterSoloMode(vivid::Operator* op, const std::string& name);
    void exitSoloMode();
    bool inSoloMode() const { return m_inSoloMode; }
    const std::string& soloOperatorName() const { return m_soloOperatorName; }

    // Focused node mode (for EditorBridge integration)
    // When cursor is in an operator's code in the editor, that node gets a 3x larger preview
    void setFocusedNode(const std::string& operatorName);
    void clearFocusedNode();
    bool isFocused(const std::string& operatorName) const;
};

} // namespace vivid::imgui
