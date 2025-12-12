#pragma once

#include <functional>
#include <string>
#include <memory>
#include <vector>

namespace vivid {

class Chain;

/// Operator info for editor communication
struct EditorOperatorInfo {
    std::string chainName;      ///< Name in chain (e.g., "noise")
    std::string displayName;    ///< Operator type (e.g., "Noise")
    std::string outputType;     ///< Output kind (e.g., "Texture")
    int sourceLine = 0;         ///< Line in chain.cpp
    std::vector<std::string> inputNames; ///< Connected input names
};

/// Parameter info for editor communication
struct EditorParamInfo {
    std::string operatorName;   ///< Owning operator's chain name
    std::string paramName;      ///< Parameter name
    std::string paramType;      ///< Type (Float, Vec3, Color, etc.)
    float value[4] = {0};       ///< Current value
    float minVal = 0.0f;        ///< Min range
    float maxVal = 1.0f;        ///< Max range
};

/// EditorBridge provides a WebSocket server for communication with external editors (VS Code, etc.)
/// Handles compile status notifications and commands like reload.
class EditorBridge {
public:
    EditorBridge();
    ~EditorBridge();

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

    /// Send operator list to all connected clients (Phase 2)
    /// @param operators Vector of operator info
    void sendOperatorList(const std::vector<EditorOperatorInfo>& operators);

    /// Send parameter values to all connected clients (Phase 2)
    /// @param params Vector of parameter info with current values
    void sendParamValues(const std::vector<EditorParamInfo>& params);

    // -------------------------------------------------------------------------
    // Incoming commands (editor -> runtime)
    // -------------------------------------------------------------------------

    /// Callback type for incoming commands
    using CommandCallback = std::function<void(const std::string& type)>;

    /// Callback type for param change commands
    using ParamChangeCallback = std::function<void(const std::string& opName,
                                                    const std::string& paramName,
                                                    const float value[4])>;

    /// Set callback for reload command
    void onReloadCommand(CommandCallback callback) { m_reloadCallback = callback; }

    /// Set callback for param change command (Phase 2)
    void onParamChange(ParamChangeCallback callback) { m_paramChangeCallback = callback; }

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_running = false;
    int m_port = 9876;
    CommandCallback m_reloadCallback;
    ParamChangeCallback m_paramChangeCallback;
};

} // namespace vivid
