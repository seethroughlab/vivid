#pragma once

// Vivid Chain Visualizer
// OverlayCanvas-based node graph for visualizing operator chains
//
// Module-agnostic: operators provide their own visualization via drawVisualization().
// No direct dependencies on render3d, audio, ImGui, or other modules.

#include <vivid/gui/input_state.h>
#include <vivid/context.h>
#include <vivid/operator.h>
#include <vivid/video_exporter.h>
#include <vivid/gui/overlay_canvas.h>
#include <vivid/gui/node_graph.h>
#include <vivid/gui/scratch_texture.h>
#include <webgpu/webgpu.h>
#include <string>
#include <unordered_map>
#include <map>
#include <vector>
#include <memory>
#include <array>
#include <functional>

namespace vivid {

class FontAtlas;  // Forward declaration for font storage

class ChainVisualizer {
public:
    ChainVisualizer();
    ~ChainVisualizer();

    // Initialize
    void init();

    // Cleanup
    void shutdown();

    // Select a node from external source (e.g., VSCode extension)
    // Will highlight the node in the graph
    void selectNodeFromEditor(const std::string& operatorName);

    // Get the currently selected operator name (for IDE sync)
    const std::string& getSelectedOperatorName() const { return m_selectedOpName; }

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

    // View state preservation for solo mode
    float m_preSoloZoom = 1.0f;
    glm::vec2 m_preSoloPan = {0, 0};

    // Double-click handling (set by callback, cleared after processing)
    int m_pendingDoubleClickNodeId = -1;

    // Output pin hover state (for tooltip)
    int m_hoveredOutputNodeId = -1;
    int m_hoveredOutputPinIndex = -1;

    // Selection helpers (for editor sync)
    void clearSelection();

    // Pending editor selection (set by selectNodeFromEditor, applied in render)
    std::string m_pendingEditorSelection;

    // Focused node mode (cursor is in operator code in editor)
    std::string m_focusedOperatorName;
    bool m_focusedModeActive = false;

    // Pending changes count (Claude-first workflow)
    size_t m_pendingChangeCount = 0;

    // MCP configuration warning
    std::string m_mcpWarning;

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
    void saveSnapshot(Context& ctx);
    bool snapshotRequested() const { return m_snapshotRequested; }

    // Solo mode control (for RuntimeAPI integration)
    void enterSoloMode(Operator* op, const std::string& name);
    void exitSoloMode();
    bool inSoloMode() const { return m_inSoloMode; }
    const std::string& soloOperatorName() const { return m_soloOperatorName; }

    // Update solo mode output texture (call before blit in app.cpp)
    // Sets ctx.outputTexture to solo operator's output when in solo mode
    void updateSoloOutput(Context& ctx);

    // Focused node mode (for RuntimeAPI integration)
    // When cursor is in an operator's code in the editor, that node gets a 3x larger preview
    void setFocusedNode(const std::string& operatorName);
    void clearFocusedNode();
    bool isFocused(const std::string& operatorName) const;

    // Pending changes indicator (Claude-first workflow)
    // Set by app.cpp each frame based on RuntimeAPI pending count
    void setPendingChangeCount(size_t count) { m_pendingChangeCount = count; }
    size_t pendingChangeCount() const { return m_pendingChangeCount; }

    // MCP configuration warning (shown if Claude Code MCP not configured)
    void setMcpWarning(const std::string& warning) { m_mcpWarning = warning; }
    const std::string& mcpWarning() const { return m_mcpWarning; }

    // Parameter change callback (for queueing pending changes)
    // Called when user adjusts a slider in the inspector panel
    // Arguments: operatorName, paramName, oldValue[4], newValue[4], sourceLine
    using ParamChangeCallback = std::function<void(const std::string&, const std::string&,
                                                    const float[4], const float[4], int)>;
    void onParamChange(ParamChangeCallback callback) { m_paramChangeCallback = callback; }

    // -------------------------------------------------------------------------
    // NodeGraph system (OverlayCanvas-based)
    // -------------------------------------------------------------------------
    void initNodeGraph(Context& ctx, WGPUTextureFormat surfaceFormat);
    void renderNodeGraph(WGPURenderPassEncoder pass, const gui::InputState& input, Context& ctx);

    // Check if visualizer consumed mouse input (for blocking input to user code)
    bool consumedInput() const { return m_nodeGraphInitialized && m_nodeGraph.consumedInput(); }

    // Check if visualizer is currently in an active interaction (pan/drag in progress)
    // Use this at the START of a frame to block input during ongoing interactions
    bool isInteracting() const { return m_nodeGraphInitialized && m_nodeGraph.isInteracting(); }

private:
    // Status bar, tooltip, debug panel, and inspector rendering (uses OverlayCanvas)
    void renderStatusBar(const gui::InputState& input, Context& ctx);
    void renderOutputPinTooltip(const gui::InputState& input, const OperatorInfo& info);
    void renderSoloIndicator(const gui::InputState& input);
    void renderDebugPanelOverlay(const gui::InputState& input, Context& ctx);
    void renderInspectorPanel(const gui::InputState& input, Context& ctx);
    void renderOperatorInspector(const gui::InputState& input, Operator* op, const std::string& title);
    void renderScreenInspector(const gui::InputState& input, Context& ctx);

    // Node graph system
    OverlayCanvas m_overlay;
    NodeGraph m_nodeGraph;
    bool m_nodeGraphInitialized = false;
    bool m_autoLayoutDone = false;
    size_t m_lastOperatorCount = 0;

    // Fonts (owned by ChainVisualizer, passed to OverlayCanvas)
    std::unique_ptr<FontAtlas> m_fonts[3];

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
    // Grid toggle
    ButtonRect m_gridToggleButton;
    // Solo mode close button
    ButtonRect m_soloCloseButton;
    bool isMouseInRect(const ButtonRect& r, glm::vec2 mousePos) const {
        return r.valid && mousePos.x >= r.x && mousePos.x < r.x + r.w &&
               mousePos.y >= r.y && mousePos.y < r.y + r.h;
    }

    // Inspector panel state
    ParamChangeCallback m_paramChangeCallback;
    bool m_inspectorVisible = true;
    float m_inspectorWidth = 280.0f;
    float m_inspectorScrollOffset = 0.0f;
    float m_inspectorContentHeight = 0.0f;

    // Inspector panel bounds for input blocking
    struct {
        float x = 0, y = 0, w = 0, h = 0;
        bool valid = false;
    } m_inspectorBounds;

    // Color picker expanded state (tracks which picker is open)
    struct ColorPickerState {
        bool expanded = false;
        std::string operatorName;
        std::string paramName;
        float originalColor[4] = {0, 0, 0, 1};
    };
    ColorPickerState m_colorPicker;

    // Active drag context for Gui widget callbacks
    // Tracks operator/param context during slider drags for change callbacks
    struct ActiveDragContext {
        std::string operatorName;
        std::string paramName;
        int sourceLine = 0;
        float originalValue[4] = {0, 0, 0, 0};
        bool active = false;
    };
    ActiveDragContext m_activeDrag;

    // Context pointer for use in node content callbacks (set during renderNodeGraph)
    Context* m_currentCtx = nullptr;

    // Scratch texture for rendering CpuPixels operators
    ScratchTexture m_cpuPixelScratch;

    // Health indicator tracking (per-node activity detection)
    std::unordered_map<int, float> m_lastRms;           // nodeId -> last known RMS for audio ops
    std::unordered_map<int, float> m_activityTimer;      // nodeId -> seconds since last change
    std::unordered_map<int, uint64_t> m_lastGeneration;  // nodeId -> last known operator generation
    float m_lastTime = 0.0f;                             // previous frame time (for dt computation)

    // Per-node activity history for sparkline rendering
    template<typename T, size_t N>
    struct RingBuffer {
        std::array<T, N> data{};
        size_t head = 0;    // next write position
        size_t count = 0;   // number of valid entries
        void push(T v) { data[head] = v; head = (head + 1) % N; if (count < N) ++count; }
        T operator[](size_t i) const { return data[(head + N - count + i) % N]; }  // oldest-first
        size_t size() const { return count; }
    };
    std::unordered_map<int, RingBuffer<float, 60>> m_activityHistory;  // nodeId -> ~1s of samples
};

} // namespace vivid
