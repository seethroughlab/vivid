#pragma once

#include <functional>
#include <string>
#include <memory>
#include <vector>
#include <deque>
#include <atomic>

namespace vivid {

class Chain;

/// Operator info for RuntimeAPI communication
struct RuntimeOperatorInfo {
    std::string chainName;      ///< Name in chain (e.g., "noise")
    std::string displayName;    ///< Operator type (e.g., "Noise")
    std::string outputType;     ///< Output kind (e.g., "Texture")
    int sourceLine = 0;         ///< Line in chain.cpp
    std::vector<std::string> inputNames; ///< Connected input names
};

/// Parameter info for RuntimeAPI communication
struct RuntimeParamInfo {
    std::string operatorName;   ///< Owning operator's chain name
    std::string paramName;      ///< Parameter name
    std::string paramType;      ///< Type (Float, Vec3, Color, FilePath, etc.)
    float value[4] = {0};       ///< Current value (for numeric types)
    float minVal = 0.0f;        ///< Min range
    float maxVal = 1.0f;        ///< Max range

    // For String/FilePath parameters
    std::string stringValue;    ///< Current string value
    std::string fileFilter;     ///< File filter pattern
    std::string fileCategory;   ///< File category hint
};

/// Per-operator timing info
struct RuntimeOperatorTiming {
    std::string name;           ///< Operator chain name
    float timeMs = 0.0f;        ///< Processing time in milliseconds
};

/// Performance metrics for RuntimeAPI
struct RuntimePerformanceStats {
    // Frame timing
    float fps = 0.0f;                           ///< Current frames per second
    float frameTimeMs = 0.0f;                   ///< Last frame time in milliseconds
    std::deque<float> fpsHistory;               ///< Recent FPS values (last 60 samples)
    std::deque<float> frameTimeHistory;         ///< Recent frame times (last 60 samples)

    // Memory usage (estimates)
    size_t textureMemoryBytes = 0;              ///< Estimated GPU texture memory
    size_t operatorCount = 0;                   ///< Number of operators in chain

    // Per-operator timing
    std::vector<RuntimeOperatorTiming> operatorTimings;
};

/// Monitor info for RuntimeAPI
struct RuntimeMonitorInfo {
    int index = 0;                              ///< Monitor index (0-based)
    std::string name;                           ///< Monitor name from GLFW
    int width = 0;                              ///< Resolution width
    int height = 0;                             ///< Resolution height
};

/// Window state for RuntimeAPI
struct RuntimeWindowState {
    bool fullscreen = false;                    ///< Fullscreen mode active
    bool borderless = false;                    ///< Borderless (undecorated) window
    bool alwaysOnTop = false;                   ///< Window stays above others
    bool cursorVisible = true;                  ///< Mouse cursor visible
    int currentMonitor = 0;                     ///< Index of monitor containing window
    std::vector<RuntimeMonitorInfo> monitors;   ///< Available monitors
};

/// Pending parameter change (slider adjustment waiting for Claude to apply)
struct PendingChange {
    std::string operatorName;                   ///< Operator chain name
    std::string paramName;                      ///< Parameter name
    std::string paramType;                      ///< Parameter type (Float, Vec3, etc.)
    float oldValue[4] = {0};                    ///< Value before change
    float newValue[4] = {0};                    ///< New value from slider
    int sourceLine = 0;                         ///< Line number in chain.cpp
    int64_t timestamp = 0;                      ///< When the change was made (ms since epoch)
};

/// RuntimeAPI provides a WebSocket server for external tools (MCP, etc.) to query and control vivid.
/// Handles compile status notifications, parameter queries, and commands like reload.
class RuntimeAPI {
public:
    RuntimeAPI();
    ~RuntimeAPI();

    /// Start the WebSocket server on the specified port
    void start(int port = 9876);

    /// Stop the WebSocket server
    void stop();

    /// Check if the server is running
    bool isRunning() const { return m_running; }

    /// Get number of connected clients
    size_t clientCount() const;

    // -------------------------------------------------------------------------
    // Outgoing messages (runtime -> editor)
    // -------------------------------------------------------------------------

    /// Send compile status to all connected clients
    /// @param success True if compilation succeeded
    /// @param message Error message if failed (with file:line:col format)
    void sendCompileStatus(bool success, const std::string& message);

    /// Get cached compile status (for late-connecting clients)
    /// @param success Output: True if last compilation succeeded
    /// @param message Output: Error message if failed
    void getCachedCompileStatus(bool& success, std::string& message) const {
        success = m_cachedCompileSuccess;
        message = m_cachedCompileMessage;
    }

    /// Send operator list to all connected clients
    /// @param operators Vector of operator info
    void sendOperatorList(const std::vector<RuntimeOperatorInfo>& operators);

    /// Send parameter values to all connected clients
    /// @param params Vector of parameter info with current values
    void sendParamValues(const std::vector<RuntimeParamInfo>& params);

    /// Send performance stats to all connected clients
    /// @param stats Performance metrics including FPS, memory, timing
    void sendPerformanceStats(const RuntimePerformanceStats& stats);

    /// Send solo mode state to all connected clients
    /// @param active True if solo mode is active
    /// @param operatorName Name of the soloed operator (empty if not active)
    void sendSoloState(bool active, const std::string& operatorName);

    /// Send window state to all connected clients
    /// @param state Current window state including fullscreen, borderless, monitors
    void sendWindowState(const RuntimeWindowState& state);

    /// Send pending changes to all connected clients
    /// Called automatically when a pending change is added/removed
    void sendPendingChanges();

    // -------------------------------------------------------------------------
    // Pending changes management (Claude-first workflow)
    // -------------------------------------------------------------------------

    /// Add a pending parameter change (from visualizer slider)
    /// @param change The pending change to queue
    void addPendingChange(const PendingChange& change);

    /// Get all pending changes
    /// @return Vector of pending changes waiting to be applied
    const std::vector<PendingChange>& getPendingChanges() const { return m_pendingChanges; }

    /// Check if there are any pending changes
    bool hasPendingChanges() const { return !m_pendingChanges.empty(); }

    /// Get count of pending changes
    size_t pendingChangeCount() const { return m_pendingChanges.size(); }

    /// Commit all pending changes (mark as applied, clear queue)
    /// Called after Claude applies changes to chain.cpp
    void commitPendingChanges();

    /// Discard all pending changes (revert to original values)
    /// Returns the changes that were discarded so caller can revert runtime state
    std::vector<PendingChange> discardPendingChanges();

    // -------------------------------------------------------------------------
    // Incoming commands (editor -> runtime)
    // -------------------------------------------------------------------------

    /// Callback type for incoming commands
    using CommandCallback = std::function<void(const std::string& type)>;

    /// Callback type for param change commands
    using ParamChangeCallback = std::function<void(const std::string& opName,
                                                    const std::string& paramName,
                                                    const float value[4])>;

    /// Callback type for solo node command
    using SoloNodeCallback = std::function<void(const std::string& operatorName)>;

    /// Callback type for solo exit command
    using SoloExitCallback = std::function<void()>;

    /// Callback type for select node command (editor -> vivid graph selection)
    using SelectNodeCallback = std::function<void(const std::string& operatorName)>;

    /// Callback type for focused node command (cursor in operator code - 3x larger preview)
    using FocusedNodeCallback = std::function<void(const std::string& operatorName)>;

    /// Callback type for request operators command (client requests current operator list)
    using RequestOperatorsCallback = std::function<void()>;

    /// Callback type for window control commands (Phase 14)
    /// @param setting Which setting to change ("fullscreen", "borderless", "alwaysOnTop", "cursorVisible", "monitor")
    /// @param value New value (bool for toggles, int for monitor index)
    using WindowControlCallback = std::function<void(const std::string& setting, int value)>;

    /// Set callback for reload command
    void onReloadCommand(CommandCallback callback) { m_reloadCallback = callback; }

    /// Set callback for param change command (Phase 2)
    void onParamChange(ParamChangeCallback callback) { m_paramChangeCallback = callback; }

    /// Set callback for solo node command
    void onSoloNode(SoloNodeCallback callback) { m_soloNodeCallback = callback; }

    /// Set callback for solo exit command
    void onSoloExit(SoloExitCallback callback) { m_soloExitCallback = callback; }

    /// Set callback for select node command (highlight in graph)
    void onSelectNode(SelectNodeCallback callback) { m_selectNodeCallback = callback; }

    /// Set callback for focused node command (cursor in operator code - 3x larger preview)
    void onFocusedNode(FocusedNodeCallback callback) { m_focusedNodeCallback = callback; }

    /// Set callback for request operators command (client requests current operator list)
    void onRequestOperators(RequestOperatorsCallback callback) { m_requestOperatorsCallback = callback; }

    /// Callback type for request compile status command
    using RequestCompileStatusCallback = std::function<void()>;

    /// Set callback for request compile status command (client requests current compile status)
    void onRequestCompileStatus(RequestCompileStatusCallback callback) { m_requestCompileStatusCallback = callback; }

    /// Set callback for window control commands (Phase 14)
    void onWindowControl(WindowControlCallback callback) { m_windowControlCallback = callback; }

    /// Callback type for discard changes command (runtime should revert values)
    using DiscardChangesCallback = std::function<void(const std::vector<PendingChange>& changes)>;

    /// Set callback for discard changes command
    /// Called when pending changes should be reverted to original values
    void onDiscardChanges(DiscardChangesCallback callback) { m_discardChangesCallback = callback; }

    /// Callback type for capture frame command (MCP live screenshot)
    /// @param outputPath Path to save the PNG file
    using CaptureFrameCallback = std::function<void(const std::string& outputPath)>;

    /// Set callback for capture frame command
    /// Called when MCP requests a screenshot of the current frame
    void onCaptureFrame(CaptureFrameCallback callback) { m_captureFrameCallback = callback; }

    // -------------------------------------------------------------------------
    // Chain structure (MCP get_chain_structure tool)
    // -------------------------------------------------------------------------

    /// Info about a single operator in the chain structure
    struct ChainOperatorInfo {
        std::string name;           ///< Operator chain name
        std::string displayName;    ///< Operator type name (e.g., "Noise")
        std::string outputType;     ///< Output kind (e.g., "Texture")
        std::vector<std::string> inputs;  ///< Input names
    };

    /// Callback type for request_chain_structure command
    /// Should return the list of operators in the running chain
    using RequestChainStructureCallback = std::function<std::vector<ChainOperatorInfo>()>;

    /// Set callback for request_chain_structure command
    void onRequestChainStructure(RequestChainStructureCallback callback) { m_requestChainStructureCallback = callback; }

    /// Send chain structure to all connected clients
    void sendChainStructure(const std::vector<ChainOperatorInfo>& operators);

    // -------------------------------------------------------------------------
    // Frame info (MCP get_frame_info tool)
    // -------------------------------------------------------------------------

    /// Frame info for MCP tool
    struct FrameInfo {
        uint64_t frame = 0;         ///< Current frame number
        double time = 0.0;          ///< Elapsed time in seconds
        float fps = 0.0f;           ///< Current FPS
    };

    /// Callback type for request_frame_info command
    using RequestFrameInfoCallback = std::function<FrameInfo()>;

    /// Set callback for request_frame_info command
    void onRequestFrameInfo(RequestFrameInfoCallback callback) { m_requestFrameInfoCallback = callback; }

    /// Send frame info to all connected clients
    void sendFrameInfo(const FrameInfo& info);

    // -------------------------------------------------------------------------
    // Reset time (MCP reset_time tool)
    // -------------------------------------------------------------------------

    /// Callback type for reset_time command
    using ResetTimeCallback = std::function<void()>;

    /// Set callback for reset_time command
    void onResetTime(ResetTimeCallback callback) { m_resetTimeCallback = callback; }

    /// Send reset_time_complete acknowledgment
    void sendResetTimeComplete();

    /// Send capture result back to clients
    /// @param success True if capture succeeded
    /// @param outputPath Path where the file was saved
    /// @param error Error message if failed
    void sendCaptureResult(bool success, const std::string& outputPath, const std::string& error = "");

    // -------------------------------------------------------------------------
    // Direct parameter control (MCP debugging tools)
    // -------------------------------------------------------------------------

    /// Callback type for set_param_immediate command (apply param directly, no pending queue)
    /// @param opName Operator name
    /// @param paramName Parameter name
    /// @param value Parameter value (float[4])
    /// @return true if param was successfully set
    using SetParamImmediateCallback = std::function<bool(const std::string& opName,
                                                          const std::string& paramName,
                                                          const float value[4])>;

    /// Set callback for set_param_immediate command
    void onSetParamImmediate(SetParamImmediateCallback callback) { m_setParamImmediateCallback = callback; }

    /// Send set_param result back to clients
    void sendSetParamResult(const std::string& opName, const std::string& paramName, bool success);

    // -------------------------------------------------------------------------
    // Frame advance control (MCP debugging tools)
    // -------------------------------------------------------------------------

    /// Request N frames to be advanced (called from WebSocket handler)
    /// Main loop should call consumePendingFrameAdvance() to get and clear this
    void requestFrameAdvance(int count);

    /// Get and clear pending frame advance count
    /// @return Number of frames to advance (0 if none pending)
    int consumePendingFrameAdvance();

    /// Send frame advance started notification
    void sendFrameAdvanceStarted(int count);

    /// Send frame advance complete notification
    /// @param newFrame The frame number after advancing
    void sendFrameAdvanceComplete(int newFrame);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_running = false;
    int m_port = 9876;
    CommandCallback m_reloadCallback;
    ParamChangeCallback m_paramChangeCallback;
    SoloNodeCallback m_soloNodeCallback;
    SoloExitCallback m_soloExitCallback;
    SelectNodeCallback m_selectNodeCallback;
    FocusedNodeCallback m_focusedNodeCallback;
    RequestOperatorsCallback m_requestOperatorsCallback;
    RequestCompileStatusCallback m_requestCompileStatusCallback;
    WindowControlCallback m_windowControlCallback;
    DiscardChangesCallback m_discardChangesCallback;
    CaptureFrameCallback m_captureFrameCallback;
    SetParamImmediateCallback m_setParamImmediateCallback;
    RequestChainStructureCallback m_requestChainStructureCallback;
    RequestFrameInfoCallback m_requestFrameInfoCallback;
    ResetTimeCallback m_resetTimeCallback;

    // Pending changes queue (Claude-first workflow)
    std::vector<PendingChange> m_pendingChanges;

    // Frame advance state (MCP debugging tools)
    std::atomic<int> m_pendingFrameAdvance{0};

    // Cached compile status (for late-connecting clients)
    bool m_cachedCompileSuccess{true};
    std::string m_cachedCompileMessage;
};

} // namespace vivid
