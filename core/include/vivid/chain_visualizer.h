#pragma once

// Vivid Chain Visualizer
// OverlayCanvas-based node graph for visualizing operator chains
//
// Addon-agnostic: operators provide their own visualization via drawVisualization().
// No direct dependencies on render3d, audio, ImGui, or other addons.

#include <vivid/frame_input.h>
#include <vivid/context.h>
#include <vivid/operator.h>
#include <vivid/video_exporter.h>
#include <vivid/overlay_canvas.h>
#include <vivid/node_graph.h>
#include <webgpu/webgpu.h>
#include <string>
#include <unordered_map>
#include <map>
#include <vector>
#include <memory>
#include <array>

namespace vivid {

class ChainVisualizer {
public:
    ChainVisualizer() = default;
    ~ChainVisualizer();

    // Initialize
    void init();

    // Cleanup
    void shutdown();

    // Select a node from external source (e.g., VSCode extension)
    // Will highlight the node in the graph
    void selectNodeFromEditor(const std::string& operatorName);

private:
    bool m_initialized = false;

    // Selection state for inspector panel
    int m_selectedNodeId = -1;
    Operator* m_selectedOp = nullptr;
    std::string m_selectedOpName;

    // Solo mode state
    Operator* m_soloOperator = nullptr;
    bool m_inSoloMode = false;
    std::string m_soloOperatorName;  // Cached for display

    // Selection helpers (for editor sync)
    void clearSelection();

    // Pending editor selection (set by selectNodeFromEditor, applied in render)
    std::string m_pendingEditorSelection;

    // Focused node mode (cursor is in operator code in editor)
    std::string m_focusedOperatorName;
    bool m_focusedModeActive = false;

    // Video recording
    VideoExporter m_exporter;
    void startRecording(ExportCodec codec, Context& ctx);
    void stopRecording(Context& ctx);

    // Snapshot
    bool m_snapshotRequested = false;
    void requestSnapshot() { m_snapshotRequested = true; }

public:
    // Access to exporter for main.cpp to call captureFrame
    VideoExporter& exporter() { return m_exporter; }

    // Save a single frame snapshot (call from main loop after rendering)
    void saveSnapshot(WGPUDevice device, WGPUQueue queue, WGPUTexture texture, Context& ctx);
    bool snapshotRequested() const { return m_snapshotRequested; }

    // Solo mode control (for EditorBridge integration)
    void enterSoloMode(Operator* op, const std::string& name);
    void exitSoloMode();
    bool inSoloMode() const { return m_inSoloMode; }
    const std::string& soloOperatorName() const { return m_soloOperatorName; }

    // Focused node mode (for EditorBridge integration)
    // When cursor is in an operator's code in the editor, that node gets a 3x larger preview
    void setFocusedNode(const std::string& operatorName);
    void clearFocusedNode();
    bool isFocused(const std::string& operatorName) const;

    // -------------------------------------------------------------------------
    // NodeGraph system (OverlayCanvas-based)
    // -------------------------------------------------------------------------
    void initNodeGraph(Context& ctx, WGPUTextureFormat surfaceFormat);
    void renderNodeGraph(WGPURenderPassEncoder pass, const FrameInput& input, Context& ctx);

    // Check if visualizer consumed mouse input (for blocking input to user code)
    bool consumedInput() const { return m_nodeGraphInitialized && m_nodeGraph.consumedInput(); }

private:
    // Status bar, tooltip, and debug panel rendering (uses OverlayCanvas)
    void renderStatusBar(const FrameInput& input, Context& ctx);
    void renderTooltip(const FrameInput& input, const OperatorInfo& info);
    void renderDebugPanelOverlay(const FrameInput& input, Context& ctx);

    // Node graph system
    OverlayCanvas m_overlay;
    NodeGraph m_nodeGraph;
    bool m_nodeGraphInitialized = false;

    // Button hit regions (set during renderStatusBar, checked for clicks after)
    struct ButtonRect { float x, y, w, h; bool valid = false; };
    ButtonRect m_recordButton;
    ButtonRect m_stopButton;
    ButtonRect m_snapshotButton;
    // Codec dropdown menu
    bool m_codecDropdownOpen = false;
    ButtonRect m_codecH264;
    ButtonRect m_codecH265;
    ButtonRect m_codecProRes;
    bool isMouseInRect(const ButtonRect& r, glm::vec2 mousePos) const {
        return r.valid && mousePos.x >= r.x && mousePos.x < r.x + r.w &&
               mousePos.y >= r.y && mousePos.y < r.y + r.h;
    }
};

} // namespace vivid
