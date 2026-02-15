// Vivid MCP Server
// Implements Model Context Protocol over stdio for Claude Code integration
// Connects to running Vivid instance via WebSocket to provide live state access

// Prevent Windows min/max macros from interfering with std::min/std::max
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <vivid/cli.h>
#include <vivid/operator_registry.h>
#include <vivid/module_loader.h>
#include <vivid/docs_search.h>
#include <vivid/audio_analysis.h>
#include <vivid/wav_writer.h>
#include <nlohmann/json.hpp>
#include <ixwebsocket/IXWebSocket.h>
#include <iostream>
#include <string>
#include <sstream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <array>
#include <csignal>
#include <regex>
#include <cmath>
#include <vivid/io/image_loader.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <dlfcn.h>
#endif

#ifdef __linux__
#include <dlfcn.h>
#endif

#ifndef _WIN32
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#else
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using json = nlohmann::json;
namespace fs = std::filesystem;

// Helper to convert ParamType to string (matches operator_registry.cpp)
static const char* paramTypeName(vivid::ParamType type) {
    switch (type) {
        case vivid::ParamType::Float:    return "Float";
        case vivid::ParamType::Int:      return "Int";
        case vivid::ParamType::Bool:     return "Bool";
        case vivid::ParamType::Vec2:     return "Vec2";
        case vivid::ParamType::Vec3:     return "Vec3";
        case vivid::ParamType::Vec4:     return "Vec4";
        case vivid::ParamType::Color:    return "Color";
        case vivid::ParamType::String:   return "String";
        case vivid::ParamType::FilePath: return "FilePath";
        default:                         return "Unknown";
    }
}

namespace vivid::mcp {

// WebSocket connection to running Vivid instance
class VividConnection {
public:
    VividConnection() = default;
    ~VividConnection() {
        stopHeartbeat();
        disconnect();
    }

    bool connect(int port = 9876) {
        std::string url = "ws://127.0.0.1:" + std::to_string(port);
        m_ws.setUrl(url);

        m_ws.setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
            if (msg->type == ix::WebSocketMessageType::Message) {
                handleMessage(msg->str);
            } else if (msg->type == ix::WebSocketMessageType::Open) {
                m_connected = true;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_lastError.clear();
                    m_lastMessageTime = std::chrono::steady_clock::now();
                }
                std::cerr << "[MCP] Connected to Vivid runtime on port 9876\n";
                // Request current state
                sendCommand("request_operators");
                sendCommand("request_pending_changes");
                sendCommand("request_compile_status");
            } else if (msg->type == ix::WebSocketMessageType::Close) {
                m_connected = false;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (!msg->closeInfo.reason.empty()) {
                        m_lastError = "Connection closed: " + msg->closeInfo.reason;
                    } else {
                        m_lastError = "Connection closed (code " + std::to_string(msg->closeInfo.code) + ")";
                    }
                }
                std::cerr << "[MCP] Disconnected from Vivid runtime: " << m_lastError << "\n";
            } else if (msg->type == ix::WebSocketMessageType::Error) {
                m_connected = false;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_lastError = "Connection error: " + msg->errorInfo.reason;
                }
                std::cerr << "[MCP] Connection error: " << msg->errorInfo.reason << "\n";
            }
        });

        m_ws.start();

        // Wait for connection (up to 2 seconds)
        for (int i = 0; i < 20 && !m_connected; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Wait for operator/param data to arrive (up to 2 seconds)
        // This is critical for reliable tool responses
        if (m_connected) {
            auto start = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(2000)) {
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    // Wait until we have BOTH operators and params
                    if (!m_operators.empty() && !m_params.empty()) {
                        std::cerr << "[MCP] Received initial state: "
                                  << m_operators.size() << " operators, "
                                  << m_params.size() << " params\n";
                        break;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            // Log warning if we timed out
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_operators.empty() || m_params.empty()) {
                std::cerr << "[MCP] Warning: Timeout waiting for data. "
                          << "operators=" << m_operators.size()
                          << " params=" << m_params.size() << "\n";
            }
        }

        // Start heartbeat to detect stale connections
        if (m_connected) {
            startHeartbeat();
        }

        return m_connected;
    }

    void disconnect() {
        stopHeartbeat();
        m_ws.stop();
        m_connected = false;
    }

    bool isConnected() const { return m_connected; }

    // Get detailed connection state for MCP responses
    struct ConnectionState {
        bool connected;
        bool stale;       // Data is from a now-dead connection
        std::string lastError;
    };

    ConnectionState getConnectionState() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        ConnectionState state;
        state.connected = m_connected.load();
        state.stale = !state.connected && m_dataReceived;
        state.lastError = m_lastError;
        return state;
    }

    void clearError() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lastError.clear();
    }

    // Start heartbeat thread to detect stale connections
    void startHeartbeat() {
        if (m_heartbeatRunning) return;

        m_heartbeatRunning = true;
        m_heartbeatThread = std::thread([this]() {
            while (m_heartbeatRunning) {
                std::this_thread::sleep_for(std::chrono::seconds(5));
                if (!m_heartbeatRunning) break;

                // Check if we've received any message in the last 15 seconds
                // (Vivid sends param_values periodically, so silence indicates a problem)
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_connected && m_dataReceived) {
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                        now - m_lastMessageTime).count();

                    if (elapsed > 15) {
                        std::cerr << "[MCP] No messages for " << elapsed << "s, connection may be stale\n";
                        // Don't set m_connected = false here, just log
                        // The WebSocket error callback will handle actual disconnection
                    }
                }
            }
        });
    }

    void stopHeartbeat() {
        m_heartbeatRunning = false;
        if (m_heartbeatThread.joinable()) {
            m_heartbeatThread.join();
        }
    }

    void sendCommand(const std::string& type) {
        json cmd;
        cmd["type"] = type;
        m_ws.send(cmd.dump());
    }

    void commitChanges() {
        sendCommand("commit_changes");
    }

    void discardChanges() {
        sendCommand("discard_changes");
    }

    // Getters for cached state
    json getOperators() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_operators;
    }

    json getParams() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_params;
    }

    json getPendingChanges() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_pendingChanges;
    }

    json getCompileStatus() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_compileStatus;
    }

    json getPerformanceStats() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_performanceStats;
    }

    json getSoloState() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_soloState;
    }

    json getWindowState() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_windowState;
    }

    json getCaptureResult() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_captureResult;
    }

    void clearCaptureResult() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_captureResult = json::object();
        m_captureResultReceived = false;
    }

    // Wait for compile status after connecting
    json waitForCompileStatus(int timeoutMs = 10000) {
        // Mark that we're waiting for compile status
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_compileStatusReceived = false;
        }

        // Request compile status
        sendCommand("request_compile_status");

        // Wait for compile_status response (up to timeout)
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(timeoutMs)) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_compileStatusReceived) {
                    return m_compileStatus;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Timeout - return whatever we have (default is success=true)
        std::lock_guard<std::mutex> lock(m_mutex);
        json result = m_compileStatus;
        result["timeout"] = true;
        return result;
    }

    // Send capture_frame command and wait for result
    json captureFrame(const std::string& outputPath, int timeoutMs = 5000) {
        // Clear any previous result
        clearCaptureResult();

        // Send command
        json cmd;
        cmd["type"] = "capture_frame";
        cmd["outputPath"] = outputPath;
        m_ws.send(cmd.dump());

        // Wait for capture_result response (up to timeout)
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(timeoutMs)) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_captureResultReceived) {
                    return m_captureResult;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // Timeout
        json result;
        result["success"] = false;
        result["error"] = "Timeout waiting for capture result";
        result["outputPath"] = outputPath;
        return result;
    }

    json captureAudio(const std::string& outputPath, float duration, int timeoutMs = 0) {
        // Clear any previous result
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_captureAudioResult = json::object();
            m_captureAudioResultReceived = false;
        }

        // Auto-compute timeout from duration + buffer
        if (timeoutMs <= 0) {
            timeoutMs = static_cast<int>((duration + 5.0f) * 1000);
        }

        json cmd;
        cmd["type"] = "capture_audio";
        cmd["outputPath"] = outputPath;
        cmd["duration"] = duration;
        m_ws.send(cmd.dump());

        auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(timeoutMs)) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_captureAudioResultReceived) {
                    return m_captureAudioResult;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        json result;
        result["success"] = false;
        result["error"] = "Timeout waiting for audio capture result";
        result["outputPath"] = outputPath;
        return result;
    }

    // Send commands to Vivid
    void soloOperator(const std::string& opName) {
        json cmd;
        cmd["type"] = "solo_node";
        cmd["operator"] = opName;
        m_ws.send(cmd.dump());
    }

    void exitSolo() {
        sendCommand("solo_exit");
    }

    void setWindowControl(const std::string& setting, int value) {
        json cmd;
        cmd["type"] = "window_control";
        cmd["setting"] = setting;
        cmd["value"] = value;
        m_ws.send(cmd.dump());
    }

    // Send set_param_immediate command and wait for result
    json setParamImmediate(const std::string& opName, const std::string& paramName,
                           const json& value, int timeoutMs = 2000) {
        // Clear any previous result
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_setParamResult = json::object();
            m_setParamResultReceived = false;
        }

        // Send command
        json cmd;
        cmd["type"] = "set_param_immediate";
        cmd["operator"] = opName;
        cmd["param"] = paramName;
        cmd["value"] = value;
        m_ws.send(cmd.dump());

        // Wait for set_param_result response
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(timeoutMs)) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_setParamResultReceived) {
                    return m_setParamResult;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Timeout
        json result;
        result["success"] = false;
        result["error"] = "Timeout waiting for set_param result";
        return result;
    }

    // Send advance_frames command and wait for completion
    json advanceFrames(int count, int timeoutMs = 30000) {
        // Clear any previous result
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_frameAdvanceResult = json::object();
            m_frameAdvanceResultReceived = false;
        }

        // Send command
        json cmd;
        cmd["type"] = "advance_frames";
        cmd["count"] = count;
        m_ws.send(cmd.dump());

        // Wait for frame_advance_complete response
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(timeoutMs)) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_frameAdvanceResultReceived) {
                    return m_frameAdvanceResult;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // Timeout
        json result;
        result["success"] = false;
        result["error"] = "Timeout waiting for frame advance";
        return result;
    }

    // Request chain structure and wait for response
    json requestChainStructure(int timeoutMs = 5000) {
        // Clear any previous result
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_chainStructure = json::object();
            m_chainStructureReceived = false;
        }

        // Send request
        sendCommand("request_chain_structure");

        // Wait for response
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(timeoutMs)) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_chainStructureReceived) {
                    return m_chainStructure;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // Timeout
        json result;
        result["error"] = "Timeout waiting for chain structure";
        return result;
    }

    // Request chain inspection and wait for response
    json requestInspectChain(bool perOperatorAnalysis = false, int timeoutMs = 10000) {
        // Clear any previous result
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_inspectChainResult = json::object();
            m_inspectChainResultReceived = false;
        }

        // Send request
        json cmd;
        cmd["type"] = "inspect_chain";
        if (perOperatorAnalysis) {
            cmd["per_operator_analysis"] = true;
        }
        m_ws.send(cmd.dump());

        // Wait for response
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(timeoutMs)) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_inspectChainResultReceived) {
                    return m_inspectChainResult;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // Timeout
        json result;
        result["error"] = "Timeout waiting for chain inspection";
        return result;
    }

    // Request frame info and wait for response
    json requestFrameInfo(int timeoutMs = 2000) {
        // Clear any previous result
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_frameInfo = json::object();
            m_frameInfoReceived = false;
        }

        // Send request
        sendCommand("request_frame_info");

        // Wait for response
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(timeoutMs)) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_frameInfoReceived) {
                    return m_frameInfo;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // Timeout
        json result;
        result["error"] = "Timeout waiting for frame info";
        return result;
    }

    // Send reset_time command and wait for acknowledgment
    json resetTime(int timeoutMs = 2000) {
        // Clear any previous result
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_resetTimeResult = json::object();
            m_resetTimeReceived = false;
        }

        // Send command
        sendCommand("reset_time");

        // Wait for response
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(timeoutMs)) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_resetTimeReceived) {
                    return m_resetTimeResult;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // Timeout - but treat as success since the command was sent
        json result;
        result["success"] = true;
        result["note"] = "Command sent, response timed out";
        return result;
    }

private:
    void handleMessage(const std::string& msgStr) {
        try {
            json msg = json::parse(msgStr);
            std::string type = msg.value("type", "");

            std::lock_guard<std::mutex> lock(m_mutex);
            m_lastMessageTime = std::chrono::steady_clock::now();
            m_dataReceived = true;

            if (type == "operator_list") {
                m_operators = msg["operators"];
            } else if (type == "param_values") {
                m_params = msg["params"];
            } else if (type == "pending_changes") {
                m_pendingChanges = msg;
            } else if (type == "compile_status") {
                m_compileStatus = msg;
                m_compileStatusReceived = true;
            } else if (type == "performance_stats") {
                m_performanceStats = msg;
            } else if (type == "solo_state") {
                m_soloState = msg;
            } else if (type == "window_state") {
                m_windowState = msg;
            } else if (type == "capture_result") {
                m_captureResult = msg;
                m_captureResultReceived = true;
            } else if (type == "set_param_result") {
                m_setParamResult = msg;
                m_setParamResultReceived = true;
            } else if (type == "frame_advance_complete") {
                m_frameAdvanceResult = msg;
                m_frameAdvanceResultReceived = true;
            } else if (type == "chain_structure") {
                m_chainStructure = msg;
                m_chainStructureReceived = true;
            } else if (type == "inspect_chain") {
                m_inspectChainResult = msg;
                m_inspectChainResultReceived = true;
            } else if (type == "frame_info") {
                m_frameInfo = msg;
                m_frameInfoReceived = true;
            } else if (type == "reset_time_complete") {
                m_resetTimeResult = msg;
                m_resetTimeReceived = true;
            } else if (type == "capture_audio_result") {
                m_captureAudioResult = msg;
                m_captureAudioResultReceived = true;
            }
        } catch (const json::exception& e) {
            std::cerr << "[MCP] JSON parse error: " << e.what() << "\n";
            std::lock_guard<std::mutex> lock(m_mutex);
            m_lastError = std::string("JSON parse error: ") + e.what();
        } catch (const std::exception& e) {
            std::cerr << "[MCP] Error handling message: " << e.what() << "\n";
            std::lock_guard<std::mutex> lock(m_mutex);
            m_lastError = std::string("Message handling error: ") + e.what();
        }
    }

    ix::WebSocket m_ws;
    std::atomic<bool> m_connected{false};
    mutable std::mutex m_mutex;

    // Cached state
    json m_operators = json::array();
    json m_params = json::array();
    json m_pendingChanges = {{"hasChanges", false}, {"changes", json::array()}};
    json m_compileStatus = {{"success", true}, {"message", ""}};
    json m_performanceStats = json::object();
    json m_soloState = {{"active", false}};
    json m_windowState = json::object();
    json m_captureResult = json::object();
    bool m_captureResultReceived{false};
    bool m_compileStatusReceived{false};
    json m_setParamResult = json::object();
    bool m_setParamResultReceived{false};
    json m_frameAdvanceResult = json::object();
    bool m_frameAdvanceResultReceived{false};
    json m_chainStructure = json::object();
    bool m_chainStructureReceived{false};
    json m_frameInfo = json::object();
    bool m_frameInfoReceived{false};
    json m_resetTimeResult = json::object();
    bool m_resetTimeReceived{false};
    json m_inspectChainResult = json::object();
    bool m_inspectChainResultReceived{false};
    json m_captureAudioResult = json::object();
    bool m_captureAudioResultReceived{false};

    // Connection state tracking
    std::string m_lastError;
    std::chrono::steady_clock::time_point m_lastMessageTime;
    bool m_dataReceived{false};  // True once we've received any data from Vivid

    // Heartbeat thread for detecting stale connections
    std::thread m_heartbeatThread;
    std::atomic<bool> m_heartbeatRunning{false};
};

// Helper to get the path to the vivid executable
std::string getVividExecutable() {
    char pathBuf[4096];
#ifdef __APPLE__
    uint32_t size = sizeof(pathBuf);
    if (_NSGetExecutablePath(pathBuf, &size) == 0) {
        return std::string(pathBuf);
    }
#elif defined(_WIN32)
    GetModuleFileNameA(nullptr, pathBuf, sizeof(pathBuf));
    return std::string(pathBuf);
#else
    ssize_t len = readlink("/proc/self/exe", pathBuf, sizeof(pathBuf) - 1);
    if (len != -1) {
        pathBuf[len] = '\0';
        return std::string(pathBuf);
    }
#endif
    return "vivid";  // Fallback to PATH
}

// Helper to run a command and capture output
struct CommandResult {
    int exitCode;
    std::string output;
    std::string error;
};

CommandResult runCommand(const std::vector<std::string>& args, int /*timeoutMs*/ = 60000) {
    CommandResult result;
    result.exitCode = -1;

    std::string command = args[0];
    for (size_t i = 1; i < args.size(); ++i) {
        command += " ";
        // Quote arguments with spaces
        if (args[i].find(' ') != std::string::npos) {
            command += "\"" + args[i] + "\"";
        } else {
            command += args[i];
        }
    }
    command += " 2>&1";  // Redirect stderr to stdout

#ifdef _WIN32
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
    if (!pipe) {
        result.error = "Failed to execute command";
        return result;
    }

    std::array<char, 256> buffer;
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result.output += buffer.data();
    }

#ifdef _WIN32
    result.exitCode = _pclose(pipe);
#else
    result.exitCode = pclose(pipe);
    result.exitCode = WEXITSTATUS(result.exitCode);
#endif

    return result;
}

// MCP Server implementation
class McpServer {
public:
    McpServer() = default;

    int run() {
        std::cerr << "[MCP] Vivid MCP Server starting...\n";

        // Try to connect to running Vivid instance
        if (!m_vivid.connect()) {
            std::cerr << "[MCP] Warning: Could not connect to Vivid runtime on port 9876\n";
            std::cerr << "[MCP] Some tools will have limited functionality\n";
        } else {
            std::cerr << "[MCP] Connected to Vivid runtime\n";
        }

        // Main loop: read JSON-RPC from stdin, write responses to stdout
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line.empty()) continue;

            try {
                json request = json::parse(line);
                json response = handleRequest(request);
                // Only send response if not empty (notifications don't get responses)
                if (!response.is_null() && !response.empty()) {
                    std::cout << response.dump() << "\n" << std::flush;
                }
            } catch (const json::exception& e) {
                json error;
                error["jsonrpc"] = "2.0";
                error["id"] = nullptr;
                error["error"] = {{"code", -32700}, {"message", "Parse error"}};
                std::cout << error.dump() << "\n" << std::flush;
            }
        }

        return 0;
    }

private:
    // Parse clang/gcc error format into structured JSON
    // Format: file:line:col: error|warning: message
    json parseCompileErrors(const std::string& rawError) {
        json errors = json::array();

        // Process line by line to avoid regex matching across context lines
        std::istringstream stream(rawError);
        std::string line;

        // Match patterns like:
        // /path/to/file.cpp:42:15: error: use of undeclared identifier 'foo'
        // /path/to/file.cpp:42:15: warning: unused variable 'x'
        // Also handles "fatal error" and "note"
        // File path must start with / or letter (Windows drive)
        std::regex errorRegex(R"(^([/A-Za-z][^:]*):(\d+):(\d+): (error|warning|fatal error|note): (.+)$)");

        while (std::getline(stream, line)) {
            std::smatch match;
            if (std::regex_match(line, match, errorRegex)) {
                json err;
                err["file"] = match[1].str();
                err["line"] = std::stoi(match[2].str());
                err["column"] = std::stoi(match[3].str());

                std::string severity = match[4].str();
                if (severity == "fatal error") severity = "error";
                err["severity"] = severity;

                err["message"] = match[5].str();
                errors.push_back(err);
            }
        }

        return errors;
    }

    json handleRequest(const json& request) {
        std::string method = request.value("method", "");
        auto id = request.value("id", json(nullptr));
        auto params = request.value("params", json::object());

        json response;
        response["jsonrpc"] = "2.0";
        response["id"] = id;

        if (method == "initialize") {
            response["result"] = handleInitialize(params);
        } else if (method == "initialized" || method == "notifications/initialized") {
            // Notification, no response needed
            return json();
        } else if (method.rfind("notifications/", 0) == 0) {
            // All notifications (methods starting with notifications/) don't need a response
            return json();
        } else if (method == "shutdown") {
            response["result"] = nullptr;
        } else if (method == "tools/list") {
            response["result"] = handleToolsList();
        } else if (method == "tools/call") {
            response["result"] = handleToolsCall(params);
        } else if (method == "resources/list") {
            response["result"] = handleResourcesList();
        } else if (method == "resources/read") {
            response["result"] = handleResourcesRead(params);
        } else {
            response["error"] = {{"code", -32601}, {"message", "Method not found"}};
        }

        return response;
    }

    json handleInitialize(const json& params) {
        json result;
        result["protocolVersion"] = "2024-11-05";
        result["serverInfo"] = {
            {"name", "vivid-mcp"},
            {"version", cli::VERSION}
        };
        result["capabilities"] = {
            {"tools", json::object()},
            {"resources", json::object()}
        };
        return result;
    }

    json handleToolsList() {
        json tools = json::array();

        // get_pending_changes - Get parameter changes waiting to be applied
        tools.push_back({
            {"name", "get_pending_changes"},
            {"description", "Get parameter changes made via sliders that are waiting to be applied to chain.cpp. Returns structured data with operator name, parameter name, old/new values, and source line number."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", json::object()}
            }}
        });

        // get_live_params - Get current parameter values
        tools.push_back({
            {"name", "get_live_params"},
            {"description", "Get real-time parameter values from the running Vivid instance. Optionally filter by operator name."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"operator", {{"type", "string"}, {"description", "Optional: filter by operator name"}}}
                }}
            }}
        });

        // clear_pending_changes - Confirm changes were applied
        tools.push_back({
            {"name", "clear_pending_changes"},
            {"description", "Clear pending changes after they have been applied to chain.cpp. Call this after editing the code."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", json::object()}
            }}
        });

        // discard_pending_changes - Revert changes
        tools.push_back({
            {"name", "discard_pending_changes"},
            {"description", "Discard pending changes and revert parameters to their original values from chain.cpp."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", json::object()}
            }}
        });

        // get_runtime_status - Get compile/runtime status
        tools.push_back({
            {"name", "get_runtime_status"},
            {"description", "Get current Vivid runtime status including connection state, compile errors, and runtime errors."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", json::object()}
            }}
        });

        // get_compile_errors - Get structured compile errors
        tools.push_back({
            {"name", "get_compile_errors"},
            {"description", "Get structured compile errors from the last chain.cpp compilation. Returns parsed errors with file, line, column, severity, and message. More actionable than raw error text."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", json::object()}
            }}
        });

        // list_operators - List available operators (from registry)
        tools.push_back({
            {"name", "list_operators"},
            {"description", "List all Vivid operators grouped by category. Returns name, module, and requiresInput. Use get_operator for full parameter details."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"verbose", {{"type", "boolean"}, {"description", "Include full parameter details (default: false, returns compact summary)"}}}
                }}
            }}
        });

        // get_operator - Get details for a specific operator
        tools.push_back({
            {"name", "get_operator"},
            {"description", "Get detailed information about a Vivid operator: parameters with types/ranges/defaults, usage example, and source module."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"name", {{"type", "string"}, {"description", "Operator name (e.g., 'Noise', 'Blur', 'Webcam')"}}}
                }},
                {"required", json::array({"name"})}
            }}
        });

        // get_example - Get working code examples for an operator
        tools.push_back({
            {"name", "get_example"},
            {"description", "Get complete, working code examples showing how to use a Vivid operator. Returns FULL chain.cpp examples from RECIPES.md (with includes, namespaces, setup/update). Use this BEFORE writing code to see correct API patterns including method calls like input(), inputA(), trigger(), etc."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"operator", {{"type", "string"}, {"description", "Operator name (e.g., 'Sequencer', 'AudioMixer', 'Displace')"}}},
                    {"snippet_only", {{"type", "boolean"}, {"description", "If true, return only the operator usage lines instead of full examples (default: false)"}}}
                }},
                {"required", json::array({"operator"})}
            }}
        });

        // get_recipe - Get complete recipe by name
        tools.push_back({
            {"name", "get_recipe"},
            {"description", "Get a complete, working chain.cpp example by recipe name. Use with no arguments to list all available recipes. Recipes are complete examples you can use as starting points or reference for correct API patterns. IMPORTANT: When writing new chains, start with a simple recipe and modify incrementally - validate_chain after each significant change!"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"name", {{"type", "string"}, {"description", "Recipe name (e.g., 'VHS/Retro Look', 'Drum Machine'). Leave empty to list all recipes."}}}
                }}
            }}
        });

        // create_project - Create new project
        tools.push_back({
            {"name", "create_project"},
            {"description", "Create a new Vivid project from a template. RECOMMENDED: Start with a working template (audio-visualizer, feedback, etc.) or use get_recipe to find a complete example similar to what you need. Modify incrementally and validate_chain frequently!"},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"name", {{"type", "string"}, {"description", "Project name"}}},
                    {"path", {{"type", "string"}, {"description", "Directory to create project in (optional, defaults to current directory)"}}},
                    {"template", {{"type", "string"}, {"description", "Template: blank, noise-demo, feedback, audio-visualizer, 3d-orbit"}}},
                    {"modules", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Modules to include (use list_modules to see available)"}}},
                    {"in_place", {{"type", "boolean"}, {"description", "If true, create files directly in path instead of path/name subdirectory. If false, force subdirectory creation. If not specified, auto-detect based on directory state."}}}
                }},
                {"required", json::array({"name"})}
            }}
        });

        // capture_snapshot - Render to PNG
        tools.push_back({
            {"name", "capture_snapshot"},
            {"description", "Render a project to PNG image(s). Useful for testing and verification."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"path", {{"type", "string"}, {"description", "Path to project directory"}}},
                    {"output", {{"type", "string"}, {"description", "Output PNG file path"}}},
                    {"frame", {{"type", "string"}, {"description", "Frame specification: single number, comma-separated list, or range (e.g., '5', '0,5,10', '0-11')"}}}
                }},
                {"required", json::array({"path", "output"})}
            }}
        });

        // validate_chain - Check if chain compiles
        tools.push_back({
            {"name", "validate_chain"},
            {"description", "Check if a project's chain.cpp compiles without running it. Returns compilation errors if any. IMPORTANT: Use this frequently during development! Validate after every 30-50 lines of new code, or after adding each new operator. Catching errors early is much easier than debugging 700 lines at once."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"path", {{"type", "string"}, {"description", "Path to project directory"}}}
                }},
                {"required", json::array({"path"})}
            }}
        });

        // bundle_project - Package as standalone app
        tools.push_back({
            {"name", "bundle_project"},
            {"description", "Bundle a Vivid project as a standalone application."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"path", {{"type", "string"}, {"description", "Path to project directory"}}},
                    {"output", {{"type", "string"}, {"description", "Output directory for the bundled app"}}},
                    {"name", {{"type", "string"}, {"description", "App display name (optional)"}}},
                    {"platform", {{"type", "string"}, {"description", "Target platform: mac, windows, linux (optional, defaults to current)"}}}
                }},
                {"required", json::array({"path"})}
            }}
        });

        // list_modules - List available modules
        tools.push_back({
            {"name", "list_modules"},
            {"description", "List installed Vivid modules. Modules are auto-linked when you #include their headers - no manual add/install step needed."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", json::object()}
            }}
        });

        // list_templates - List available project templates
        tools.push_back({
            {"name", "list_templates"},
            {"description", "List available project templates with descriptions. Use with create_project."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", json::object()}
            }}
        });

        // search_docs - Search documentation
        tools.push_back({
            {"name", "search_docs"},
            {"description", "Search Vivid documentation including core docs (RECIPES, CHAIN-API, etc.), module READMEs (Audio, Video, Render3D, etc.), and example AGENTS.md files. Returns matching sections."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"query", {{"type", "string"}, {"description", "Search query (case-insensitive)"}}}
                }},
                {"required", json::array({"query"})}
            }}
        });

        // get_performance_stats - Get real-time performance metrics
        tools.push_back({
            {"name", "get_performance_stats"},
            {"description", "Get real-time performance metrics from running Vivid: FPS, frame time, per-operator timing, texture memory usage. Use to identify slow operators or performance issues."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", json::object()}
            }}
        });

        // solo_operator - Isolate a single operator's output
        tools.push_back({
            {"name", "solo_operator"},
            {"description", "Solo an operator to see only its output (bypass the rest of the chain). Useful for debugging individual operators."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"name", {{"type", "string"}, {"description", "Operator name to solo (e.g., 'noise', 'blur')"}}}
                }},
                {"required", json::array({"name"})}
            }}
        });

        // exit_solo - Return to normal chain output
        tools.push_back({
            {"name", "exit_solo"},
            {"description", "Exit solo mode and return to normal chain output."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", json::object()}
            }}
        });

        // get_solo_state - Get current solo state
        tools.push_back({
            {"name", "get_solo_state"},
            {"description", "Check if solo mode is active and which operator is soloed."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", json::object()}
            }}
        });

        // get_window_state - Get current window configuration
        tools.push_back({
            {"name", "get_window_state"},
            {"description", "Get current window configuration: fullscreen, borderless, monitors, etc."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", json::object()}
            }}
        });

        // set_window_mode - Control window mode
        tools.push_back({
            {"name", "set_window_mode"},
            {"description", "Set window mode: fullscreen, windowed, borderless, always-on-top, or hide cursor."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"setting", {{"type", "string"}, {"description", "Setting: 'fullscreen', 'borderless', 'alwaysOnTop', 'cursor'"}}},
                    {"value", {{"type", "boolean"}, {"description", "Enable (true) or disable (false) the setting"}}}
                }},
                {"required", json::array({"setting", "value"})}
            }}
        });

        // capture_frame - Capture current frame from running Vivid
        tools.push_back({
            {"name", "capture_frame"},
            {"description", "Capture the current frame from a running Vivid instance to a PNG file. Unlike capture_snapshot which spawns a new process, this captures from the live running instance immediately. Use this to verify visual output after making changes. When devtools are active, captures the full composited frame including UI panels."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"outputPath", {{"type", "string"}, {"description", "Path to save the PNG file (default: /tmp/vivid_capture.png)"}}}
                }}
            }}
        });

        // set_param - Set parameter on running operator immediately
        tools.push_back({
            {"name", "set_param"},
            {"description", "Set a parameter on a running operator immediately. Changes are applied directly without going through pending changes queue. Use for interactive debugging and experimentation."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"operator", {{"type", "string"}, {"description", "Operator name (e.g., 'noise', 'camera')"}}},
                    {"param", {{"type", "string"}, {"description", "Parameter name (e.g., 'scale', 'distance')"}}},
                    {"value", {{"description", "Number or array [x,y,z,w] for vector parameters"}}}
                }},
                {"required", json::array({"operator", "param", "value"})}
            }}
        });

        // advance_frames - Advance simulation by N frames
        tools.push_back({
            {"name", "advance_frames"},
            {"description", "Advance the simulation by N frames without rendering to display. Use for testing animations or reaching a specific point in time."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"count", {{"type", "integer"}, {"description", "Number of frames to advance"}}}
                }},
                {"required", json::array({"count"})}
            }}
        });

        // orbit_camera - Position camera around a target
        tools.push_back({
            {"name", "orbit_camera"},
            {"description", "Position the camera to orbit around a target point. Sets center, distance, azimuth, and elevation parameters on the camera operator."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"operator", {{"type", "string"}, {"description", "Camera operator name (default: 'camera')"}}},
                    {"target", {{"type", "array"}, {"items", {{"type", "number"}}}, {"description", "[x, y, z] point to look at"}}},
                    {"distance", {{"type", "number"}, {"description", "Distance from target"}}},
                    {"azimuth", {{"type", "number"}, {"description", "Horizontal angle in degrees (0-360)"}}},
                    {"elevation", {{"type", "number"}, {"description", "Vertical angle in degrees (-90 to 90)"}}}
                }},
                {"required", json::array({"target", "distance"})}
            }}
        });

        // capture_at_frame - Advance to frame N and capture
        tools.push_back({
            {"name", "capture_at_frame"},
            {"description", "Advance simulation to a specific frame number and capture a snapshot. Combines advance_frames and capture_frame for convenient debugging."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"frame", {{"type", "integer"}, {"description", "Target frame number to capture at"}}},
                    {"outputPath", {{"type", "string"}, {"description", "Path to save the PNG file (default: /tmp/vivid_capture.png)"}}}
                }},
                {"required", json::array({"frame"})}
            }}
        });

        // sweep_param - Sweep a parameter across a range of values
        tools.push_back({
            {"name", "sweep_param"},
            {"description", "Sweep a parameter across a range of values, capturing a frame at each step. "
                            "Returns paths to all captured images. Restores the original value when done."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"operator", {{"type", "string"}, {"description", "Operator name (e.g., 'noise')"}}},
                    {"param", {{"type", "string"}, {"description", "Parameter name (e.g., 'scale')"}}},
                    {"from", {{"type", "number"}, {"description", "Starting value"}}},
                    {"to", {{"type", "number"}, {"description", "Ending value"}}},
                    {"steps", {{"type", "integer"}, {"description", "Number of steps (2-20, default: 5)"}}},
                    {"settle_frames", {{"type", "integer"}, {"description", "Frames to advance between captures for effects to settle (default: 3)"}}},
                    {"output_dir", {{"type", "string"}, {"description", "Directory for output PNGs (default: /tmp/vivid_sweep)"}}}
                }},
                {"required", json::array({"operator", "param", "from", "to"})}
            }}
        });

        // compare_frames - Compare two PNG images
        tools.push_back({
            {"name", "compare_frames"},
            {"description", "Compare two PNG images and return similarity metrics (RMSE, per-channel diff, "
                            "changed pixel percentage). Works with any PNG files — no running Vivid needed."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"image_a", {{"type", "string"}, {"description", "Path to first PNG image"}}},
                    {"image_b", {{"type", "string"}, {"description", "Path to second PNG image"}}},
                    {"threshold", {{"type", "integer"}, {"description", "Per-channel diff threshold for pixel counting (0-255, default: 5)"}}}
                }},
                {"required", json::array({"image_a", "image_b"})}
            }}
        });

        // capture_audio - Capture audio from running Vivid
        tools.push_back({
            {"name", "capture_audio"},
            {"description", "Capture audio from a running Vivid instance to a WAV file. Records from the chain's audio output for the specified duration. Returns analysis metrics (RMS, peak, spectrum). Requires a running Vivid instance with an audio output."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"outputPath", {{"type", "string"}, {"description", "Path to save the WAV file (default: /tmp/vivid_capture.wav)"}}},
                    {"duration", {{"type", "number"}, {"description", "Duration in seconds to capture (default: 1.0, max: 30.0)"}}}
                }}
            }}
        });

        // sweep_param_audio - Sweep a parameter capturing audio at each step
        tools.push_back({
            {"name", "sweep_param_audio"},
            {"description", "Sweep a parameter across values, capturing audio at each step. "
                            "Returns WAV paths and audio analysis (RMS, spectrum) per step. "
                            "Restores the original value when done. Requires a running Vivid instance with audio output."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"operator", {{"type", "string"}, {"description", "Operator name (e.g., 'osc')"}}},
                    {"param", {{"type", "string"}, {"description", "Parameter name (e.g., 'frequency')"}}},
                    {"from", {{"type", "number"}, {"description", "Starting value"}}},
                    {"to", {{"type", "number"}, {"description", "Ending value"}}},
                    {"steps", {{"type", "integer"}, {"description", "Number of steps (2-10, default: 5)"}}},
                    {"audio_duration", {{"type", "number"}, {"description", "Seconds of audio to capture per step (default: 0.5, max: 5.0)"}}},
                    {"settle_frames", {{"type", "integer"}, {"description", "Frames to advance before capturing for audio to settle (default: 3)"}}},
                    {"output_dir", {{"type", "string"}, {"description", "Directory for output WAV files (default: /tmp/vivid_sweep_audio)"}}}
                }},
                {"required", json::array({"operator", "param", "from", "to"})}
            }}
        });

        // compare_audio - Compare two WAV files
        tools.push_back({
            {"name", "compare_audio"},
            {"description", "Compare two WAV audio files and return similarity metrics (RMS diff, peak diff, "
                            "per-band spectral diff, correlation). Works with any WAV files — no running Vivid needed."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"audio_a", {{"type", "string"}, {"description", "Path to first WAV file"}}},
                    {"audio_b", {{"type", "string"}, {"description", "Path to second WAV file"}}}
                }},
                {"required", json::array({"audio_a", "audio_b"})}
            }}
        });

        // list_project_assets - List assets in project folder
        tools.push_back({
            {"name", "list_project_assets"},
            {"description", "List assets in project's assets/ folder (images, audio, models, fonts, videos). Use this to know what files are available before referencing them in chain.cpp."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"path", {{"type", "string"}, {"description", "Path to project directory"}}}
                }},
                {"required", json::array({"path"})}
            }}
        });

        // get_chain_structure - Get running chain's operators and connections
        tools.push_back({
            {"name", "get_chain_structure"},
            {"description", "Get the structure of the running chain: operators, their types, input connections, and output types. Use this to understand what a project does without parsing code."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", json::object()}
            }}
        });

        // inspect_chain - Full introspection of running chain
        tools.push_back({
            {"name", "inspect_chain"},
            {"description", "Get full introspection data from the running chain: per-operator metrics (params, computed values like rms/peak/energy), plus statistical analysis of the output texture (brightness, contrast, dominant color, histogram, spatial distribution). Use this to evaluate visual output and understand chain state for autonomous iteration. Pass per_operator_analysis=true to include texture analysis at every node (helps diagnose where brightness/contrast issues originate)."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"per_operator_analysis", {
                        {"type", "boolean"},
                        {"description", "Include texture analysis (brightness, contrast, histogram) for each operator's output. Helps diagnose where in the chain visual issues originate. Default: false."}
                    }}
                }}
            }}
        });

        // wait_for_reload - Wait for hot-reload to complete
        tools.push_back({
            {"name", "wait_for_reload"},
            {"description", "Wait for hot-reload to complete after editing chain.cpp. Blocks until compilation finishes (success or error) or timeout. Use after editing code to verify changes compiled successfully."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"timeout", {{"type", "integer"}, {"description", "Timeout in milliseconds (default: 10000)"}}}
                }}
            }}
        });

        // get_frame_info - Get current animation state
        tools.push_back({
            {"name", "get_frame_info"},
            {"description", "Get current animation state: frame number, elapsed time, and FPS. Use to know where in the animation timeline you are before capturing."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", json::object()}
            }}
        });

        // reset_time - Reset animation to frame 0
        tools.push_back({
            {"name", "reset_time"},
            {"description", "Reset the animation to frame 0 and time 0. Use to start fresh before capturing or testing."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", json::object()}
            }}
        });

        // save_preset - Save parameter snapshot
        tools.push_back({
            {"name", "save_preset"},
            {"description", "Save current parameter values to a named preset file. Presets are stored in project/presets/ directory."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"path", {{"type", "string"}, {"description", "Path to project directory"}}},
                    {"name", {{"type", "string"}, {"description", "Preset name (without extension)"}}}
                }},
                {"required", json::array({"path", "name"})}
            }}
        });

        // load_preset - Load parameter snapshot
        tools.push_back({
            {"name", "load_preset"},
            {"description", "Load parameter values from a saved preset. Applies all parameters to the running instance."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"path", {{"type", "string"}, {"description", "Path to project directory"}}},
                    {"name", {{"type", "string"}, {"description", "Preset name (without extension)"}}}
                }},
                {"required", json::array({"path", "name"})}
            }}
        });

        // list_snapshots - List saved snapshots
        tools.push_back({
            {"name", "list_snapshots"},
            {"description", "List all saved snapshots for a project. Snapshots capture all parameter values across the entire chain for instant recall."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"path", {{"type", "string"}, {"description", "Path to project directory"}}}
                }},
                {"required", json::array({"path"})}
            }}
        });

        // save_snapshot - Capture current params as snapshot
        tools.push_back({
            {"name", "save_snapshot"},
            {"description", "Capture current parameter values from the running Vivid instance as a named snapshot. Snapshots are saved to vivid-snapshots.json in the project directory."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"path", {{"type", "string"}, {"description", "Path to project directory"}}},
                    {"name", {{"type", "string"}, {"description", "Snapshot name"}}}
                }},
                {"required", json::array({"path", "name"})}
            }}
        });

        // recall_snapshot - Apply a snapshot
        tools.push_back({
            {"name", "recall_snapshot"},
            {"description", "Apply a saved snapshot to the running Vivid instance. Sets all parameter values from the snapshot. Optional crossfade duration for smooth transitions."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"path", {{"type", "string"}, {"description", "Path to project directory"}}},
                    {"name", {{"type", "string"}, {"description", "Snapshot name (or index as string)"}}},
                    {"crossfade", {{"type", "number"}, {"description", "Crossfade duration in seconds (default: 0 = hard cut)"}}}
                }},
                {"required", json::array({"path", "name"})}
            }}
        });

        // delete_snapshot - Remove a snapshot
        tools.push_back({
            {"name", "delete_snapshot"},
            {"description", "Delete a snapshot from the project's vivid-snapshots.json file."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"path", {{"type", "string"}, {"description", "Path to project directory"}}},
                    {"name", {{"type", "string"}, {"description", "Snapshot name (or index as string)"}}}
                }},
                {"required", json::array({"path", "name"})}
            }}
        });

        // export_video - Export video from a project
        tools.push_back({
            {"name", "export_video"},
            {"description", "Export a Vivid project to video. Runs headless rendering with optional playback script for scripted parameter changes and events. Returns structured JSON with output path, file size, and any warnings. Use --script with a vivid-project.json events file to automate parameter changes during export."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"path", {{"type", "string"}, {"description", "Path to project directory"}}},
                    {"output", {{"type", "string"}, {"description", "Output video file path (e.g., output.mp4)"}}},
                    {"duration", {{"type", "number"}, {"description", "Duration in seconds"}}},
                    {"script", {{"type", "string"}, {"description", "Path to playback script JSON file (vivid-project.json with events)"}}},
                    {"fps", {{"type", "number"}, {"description", "Frame rate (default: 60)"}}},
                    {"codec", {{"type", "string"}, {"description", "Video codec: h264, h265, prores (default: h264)"}}},
                    {"audio", {{"type", "boolean"}, {"description", "Include audio track (default: false)"}}},
                    {"resolution", {{"type", "string"}, {"description", "Render resolution (e.g., 1920x1080)"}}}
                }},
                {"required", json::array({"path", "output", "duration"})}
            }}
        });

        // run_project - Launch Vivid in the background
        tools.push_back({
            {"name", "run_project"},
            {"description", "Launch a Vivid project in a background window and connect via WebSocket. Only one instance can run at a time. After launching, all live MCP tools (get_live_params, set_param, capture_frame, etc.) become available. Returns connected:true when WebSocket is ready."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"path", {{"type", "string"}, {"description", "Path to project directory"}}},
                    {"show_ui", {{"type", "boolean"}, {"description", "Show devtools UI (default: true)"}}},
                    {"resolution", {{"type", "string"}, {"description", "Window resolution (e.g., 1280x720)"}}}
                }},
                {"required", json::array({"path"})}
            }}
        });

        // stop_project - Stop a running Vivid instance
        tools.push_back({
            {"name", "stop_project"},
            {"description", "Stop a Vivid instance that was launched by run_project. Sends graceful shutdown signal, then force-kills if needed. Disconnects WebSocket."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", json::object()}
            }}
        });

        return {{"tools", tools}};
    }

    // Helper to wrap tool results with connection state
    // This ensures Claude Code always knows if data is live or stale
    json makeToolResult(const json& data, bool requiresConnection = false) {
        auto connState = m_vivid.getConnectionState();

        json result;
        result["isError"] = false;

        // Build response with connection state
        json response;
        response["connected"] = connState.connected;

        if (!connState.connected && requiresConnection) {
            // Tool requires connection but we're disconnected
            response["stale"] = connState.stale;
            if (!connState.lastError.empty()) {
                response["error"] = connState.lastError;
            } else {
                response["error"] = "Vivid not running on port 9876";
            }
            response["suggestion"] = "Run: ./build/bin/vivid <project>";
            response["data"] = data;  // Still include cached data, but mark as stale
        } else {
            response["data"] = data;
        }

        result["content"] = {{
            {"type", "text"},
            {"text", response.dump(2)}
        }};
        return result;
    }

    json handleToolsCall(const json& params) {
        std::string name = params.value("name", "");
        auto args = params.value("arguments", json::object());

        json result;
        result["isError"] = false;

        if (name == "get_pending_changes") {
            json data = m_vivid.getPendingChanges();
            return makeToolResult(data, true);  // Requires connection
        }
        else if (name == "get_live_params") {
            json liveParams = m_vivid.getParams();
            std::string opFilter = args.value("operator", "");

            if (!opFilter.empty()) {
                json filtered = json::array();
                for (const auto& p : liveParams) {
                    if (p.value("operator", "") == opFilter) {
                        filtered.push_back(p);
                    }
                }
                liveParams = filtered;
            }

            return makeToolResult(liveParams, true);  // Requires connection
        }
        else if (name == "clear_pending_changes") {
            auto connState = m_vivid.getConnectionState();
            if (!connState.connected) {
                json response;
                response["success"] = false;
                response["connected"] = false;
                response["error"] = connState.lastError.empty() ?
                    "Cannot clear changes: Vivid not running" : connState.lastError;
                response["suggestion"] = "Run: ./build/bin/vivid <project>";
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                return result;
            }
            m_vivid.commitChanges();
            json response;
            response["success"] = true;
            response["connected"] = true;
            response["message"] = "Pending changes cleared.";
            result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
            return result;
        }
        else if (name == "discard_pending_changes") {
            auto connState = m_vivid.getConnectionState();
            if (!connState.connected) {
                json response;
                response["success"] = false;
                response["connected"] = false;
                response["error"] = connState.lastError.empty() ?
                    "Cannot discard changes: Vivid not running" : connState.lastError;
                response["suggestion"] = "Run: ./build/bin/vivid <project>";
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                return result;
            }
            m_vivid.discardChanges();
            json response;
            response["success"] = true;
            response["connected"] = true;
            response["message"] = "Pending changes discarded. Parameters reverted to original values.";
            result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
            return result;
        }
        else if (name == "get_runtime_status") {
            auto connState = m_vivid.getConnectionState();
            json status;
            status["connected"] = connState.connected;
            status["stale"] = connState.stale;
            if (!connState.lastError.empty()) {
                status["lastError"] = connState.lastError;
            }
            status["compileStatus"] = m_vivid.getCompileStatus();
            json operators = m_vivid.getOperators();
            status["operators"] = operators;
            status["pendingChanges"] = m_vivid.getPendingChanges()["hasChanges"];

            // Collect operator errors for easy access
            json operatorErrors = json::array();
            for (const auto& op : operators) {
                if (op.contains("error") && !op["error"].get<std::string>().empty()) {
                    operatorErrors.push_back({
                        {"operator", op["name"]},
                        {"error", op["error"]}
                    });
                }
            }
            if (!operatorErrors.empty()) {
                status["operatorErrors"] = operatorErrors;
            }

            result["content"] = {{
                {"type", "text"},
                {"text", status.dump(2)}
            }};
        }
        else if (name == "get_compile_errors") {
            auto connState = m_vivid.getConnectionState();
            json response;
            response["connected"] = connState.connected;

            if (!connState.connected) {
                response["error"] = "Vivid not running";
                response["suggestion"] = "Run: ./build/bin/vivid <project>";
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                return result;
            }

            json compileStatus = m_vivid.getCompileStatus();
            bool success = compileStatus.value("success", true);
            std::string rawMessage = compileStatus.value("message", "");

            response["success"] = success;

            if (success) {
                response["errors"] = json::array();
                response["message"] = "Compilation successful";
            } else {
                // Parse the raw error message into structured format
                json errors = parseCompileErrors(rawMessage);
                response["errors"] = errors;
                response["errorCount"] = errors.size();

                // Include raw message for reference (in case parsing missed something)
                if (!rawMessage.empty()) {
                    response["raw"] = rawMessage;
                }

                // If we couldn't parse any errors but there was a failure, include raw
                if (errors.empty() && !rawMessage.empty()) {
                    response["warning"] = "Could not parse error format, see 'raw' field";
                }
            }

            result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
        }
        else if (name == "list_operators") {
            bool verbose = args.value("verbose", false);
            auto& registry = OperatorRegistry::instance();

            if (verbose) {
                // Full output with all parameter details
                json opList = registry.toJsonGrouped();
                result["content"] = {{{"type", "text"}, {"text", opList.dump(2)}}};
            } else {
                // Compact output: just name, category, module, requiresInput
                json compact = json::object();
                for (const auto& meta : registry.operators()) {
                    if (compact.find(meta.category) == compact.end()) {
                        compact[meta.category] = json::array();
                    }
                    compact[meta.category].push_back({
                        {"name", meta.name},
                        {"module", meta.module.empty() ? json(nullptr) : json(meta.module)},
                        {"requiresInput", meta.requiresInput}
                    });
                }
                result["content"] = {{{"type", "text"}, {"text", compact.dump(2)}}};
            }
        }
        else if (name == "get_operator") {
            std::string opName = args.value("name", "");
            if (opName.empty()) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Operator name is required"}}};
                return result;
            }

            auto& registry = OperatorRegistry::instance();
            const OperatorMeta* meta = registry.find(opName);

            // Case-insensitive fallback search
            if (!meta) {
                std::string opLower = opName;
                std::transform(opLower.begin(), opLower.end(), opLower.begin(), ::tolower);
                for (const auto& m : registry.operators()) {
                    std::string nameLower = m.name;
                    std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
                    if (nameLower == opLower) {
                        meta = &m;
                        break;
                    }
                }
            }

            if (!meta) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"},
                    {"text", "Operator '" + opName + "' not found. Use list_operators to see available operators."}}};
                return result;
            }

            // Build response
            json info;
            info["name"] = meta->name;
            info["category"] = meta->category;
            info["description"] = meta->description;
            info["module"] = meta->module.empty() ? "vivid-core" : meta->module;
            info["headerPath"] = meta->headerPath;

            // Add include guidance for non-core modules
            if (!meta->module.empty()) {
                // Map module name to namespace: vivid-video -> video, vivid-render3d -> render3d
                std::string ns = meta->module;
                if (ns.rfind("vivid-", 0) == 0) ns = ns.substr(6);
                info["include"] = "#include <vivid/" + ns + "/" + ns + ".h>";
            }
            info["requiresInput"] = meta->requiresInput;
            info["outputType"] = outputKindName(meta->outputKind);
            info["params"] = json::array();

            // Get parameters via factory
            if (meta->factory) {
                try {
                    auto tempOp = meta->factory();
                    auto params = tempOp->params();
                    for (const auto& p : params) {
                        json param;
                        param["name"] = p.name;
                        param["type"] = paramTypeName(p.type);

                        // Format default based on type
                        if (p.type == ParamType::String || p.type == ParamType::FilePath) {
                            param["default"] = p.stringDefault;
                            if (!p.fileFilter.empty()) param["fileFilter"] = p.fileFilter;
                        } else if (p.type == ParamType::Bool) {
                            param["default"] = (p.defaultVal[0] != 0.0f);
                        } else if (p.type == ParamType::Int) {
                            param["default"] = static_cast<int>(p.defaultVal[0]);
                            param["min"] = static_cast<int>(p.minVal);
                            param["max"] = static_cast<int>(p.maxVal);
                        } else if (p.type == ParamType::Vec2) {
                            param["default"] = json::array({p.defaultVal[0], p.defaultVal[1]});
                        } else if (p.type == ParamType::Vec3 || p.type == ParamType::Color) {
                            param["default"] = json::array({p.defaultVal[0], p.defaultVal[1], p.defaultVal[2]});
                        } else if (p.type == ParamType::Vec4) {
                            param["default"] = json::array({p.defaultVal[0], p.defaultVal[1], p.defaultVal[2], p.defaultVal[3]});
                        } else {
                            // Float
                            param["default"] = p.defaultVal[0];
                            param["min"] = p.minVal;
                            param["max"] = p.maxVal;
                        }
                        info["params"].push_back(param);
                    }
                } catch (...) {
                    info["params_error"] = "Could not introspect parameters";
                }
            }

            // Add usage hint
            info["usage_hint"] = "Use get_example with this operator name to see complete working code.";

            result["content"] = {{{"type", "text"}, {"text", info.dump(2)}}};
        }
        else if (name == "get_example") {
            std::string opName = args.value("operator", "");
            if (opName.empty()) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Operator name is required"}}};
                return result;
            }

            auto response = vivid::docs::findExamples(opName);
            result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
        }
        else if (name == "get_recipe") {
            std::string recipeName = args.value("name", "");
            auto response = vivid::docs::getRecipes(recipeName);

            if (response.contains("error")) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", response["error"].get<std::string>()}}};
            } else {
                // Add MCP-specific hints
                if (recipeName.empty()) {
                    response["hint"] = "Use get_recipe with a name to get the full code. Start with a simple recipe and modify incrementally!";
                } else {
                    response["hint"] = "This is a complete, working example. After modifying, use validate_chain to check for errors!";
                }
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
            }
        }
        else if (name == "create_project") {
            std::string projectName = args.value("name", "");
            std::string targetPath = args.value("path", ".");
            std::string templateName = args.value("template", "blank");

            if (projectName.empty()) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Project name is required"}}};
                return result;
            }

            // Determine in_place mode: explicit, or let CLI auto-detect
            bool hasExplicitInPlace = args.contains("in_place") && args["in_place"].is_boolean();
            bool explicitInPlace = hasExplicitInPlace ? args["in_place"].get<bool>() : false;

            std::vector<std::string> cmdArgs = {
                getVividExecutable(), "new", projectName,
                "-y",  // Skip prompts
                "-t", templateName
            };

            // Add in-place flags if explicitly specified
            if (hasExplicitInPlace) {
                if (explicitInPlace) {
                    cmdArgs.push_back("--in-place");
                } else {
                    cmdArgs.push_back("--no-in-place");
                }
            }
            // If not specified, CLI will auto-detect based on directory state

            // Add modules if specified
            if (args.contains("modules") && args["modules"].is_array()) {
                std::string moduleList;
                for (const auto& mod : args["modules"]) {
                    if (!moduleList.empty()) moduleList += ",";
                    moduleList += mod.get<std::string>();
                }
                if (!moduleList.empty()) {
                    cmdArgs.push_back("-m");
                    cmdArgs.push_back(moduleList);
                }
            }

            // Validate target path exists
            if (!fs::exists(targetPath)) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Directory does not exist: " + targetPath}}};
                return result;
            }

            // Change to target directory for the command
            std::string origDir = fs::current_path().string();
            try {
                fs::current_path(targetPath);
            } catch (...) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Invalid path: " + targetPath}}};
                return result;
            }

            auto cmdResult = runCommand(cmdArgs);
            fs::current_path(origDir);  // Restore directory

            if (cmdResult.exitCode == 0) {
                // Determine actual project path based on CLI output
                // If in-place was used, project is at targetPath, otherwise targetPath/projectName
                fs::path projectPath;
                if (cmdResult.output.find("Created chain.cpp") != std::string::npos ||
                    cmdResult.output.find("Created ./chain.cpp") != std::string::npos) {
                    // In-place creation - files are in targetPath
                    projectPath = fs::path(targetPath);
                } else {
                    // Subdirectory creation
                    projectPath = fs::path(targetPath) / projectName;
                }

                json response;
                response["success"] = true;
                response["path"] = fs::absolute(projectPath).string();
                response["output"] = cmdResult.output;
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
            } else {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Failed to create project:\n" + cmdResult.output}}};
            }
        }
        else if (name == "capture_snapshot") {
            std::string projectPath = args.value("path", "");
            std::string outputPath = args.value("output", "");
            std::string frameSpec = args.value("frame", "5");

            if (projectPath.empty() || outputPath.empty()) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Both path and output are required"}}};
                return result;
            }

            std::vector<std::string> cmdArgs = {
                getVividExecutable(), projectPath,
                "--snapshot", outputPath,
                "--snapshot-frame", frameSpec
            };

            auto cmdResult = runCommand(cmdArgs, 120000);  // 2 minute timeout for rendering

            if (cmdResult.exitCode == 0) {
                json response;
                response["success"] = true;
                response["output"] = outputPath;
                response["log"] = cmdResult.output;
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
            } else {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Snapshot failed:\n" + cmdResult.output}}};
            }
        }
        else if (name == "validate_chain") {
            std::string projectPath = args.value("path", "");

            if (projectPath.empty()) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Project path is required"}}};
                return result;
            }

            // Use snapshot with frame 0 and null output to just compile
#ifdef _WIN32
            std::string nullDevice = "NUL";
#else
            std::string nullDevice = "/dev/null";
#endif
            std::vector<std::string> cmdArgs = {
                getVividExecutable(), projectPath,
                "--snapshot", nullDevice,
                "--snapshot-frame", "0"
            };

            auto cmdResult = runCommand(cmdArgs, 60000);

            // Parse errors from output regardless of exit code
            // (hot-reload may succeed with cached code even when new code has errors)
            json errors = parseCompileErrors(cmdResult.output);

            json response;
            // Valid only if exit code is 0 AND no errors were parsed
            bool hasErrors = !errors.empty();
            bool exitOk = (cmdResult.exitCode == 0);
            response["valid"] = exitOk && !hasErrors;

            if (hasErrors) {
                response["errors"] = errors;
                response["errorCount"] = errors.size();
            }
            if (!exitOk) {
                response["exitCode"] = cmdResult.exitCode;
            }
            // Include raw output if there were issues
            if (!exitOk || hasErrors) {
                response["raw"] = cmdResult.output;
            }
            // Add helpful message
            if (response["valid"]) {
                response["message"] = "Compilation successful";
            } else {
                response["message"] = "Compilation failed. See 'errors' for details.";
                response["suggestion"] = "Fix the errors and try again. After editing, use get_runtime_status to verify hot-reload succeeded.";
            }
            result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
        }
        else if (name == "bundle_project") {
            std::string projectPath = args.value("path", "");
            std::string outputDir = args.value("output", "");
            std::string appName = args.value("name", "");
            std::string platform = args.value("platform", "");

            if (projectPath.empty()) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Project path is required"}}};
                return result;
            }

            std::vector<std::string> cmdArgs = {
                getVividExecutable(), "bundle", projectPath
            };

            if (!outputDir.empty()) {
                cmdArgs.push_back("-o");
                cmdArgs.push_back(outputDir);
            }
            if (!appName.empty()) {
                cmdArgs.push_back("-n");
                cmdArgs.push_back(appName);
            }
            if (!platform.empty()) {
                cmdArgs.push_back("-p");
                cmdArgs.push_back(platform);
            }

            auto cmdResult = runCommand(cmdArgs, 300000);  // 5 minute timeout for bundling

            if (cmdResult.exitCode == 0) {
                json response;
                response["success"] = true;
                response["output"] = cmdResult.output;
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
            } else {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Bundle failed:\n" + cmdResult.output}}};
            }
        }
        else if (name == "list_modules") {
            json output = json::object();

            // Built-in modules (always available)
            output["builtin"] = json::array();
            const std::vector<std::pair<std::string, std::string>> builtins = {
                {"vivid-audio", "Audio synthesis, sequencing, FFT analysis, and drum machines"},
                {"vivid-video", "Video playback with HAP, H.264, ProRes, and webcam input"},
                {"vivid-render3d", "3D rendering with PBR, GLTF loading, CSG operations"},
                {"vivid-network", "OSC, UDP, and WebSocket communication"},
                {"vivid-serial", "Serial port I/O and DMX lighting control"},
                {"vivid-midi", "MIDI input/output and file playback"}
            };
            for (const auto& [modName, desc] : builtins) {
                output["builtin"].push_back({
                    {"name", modName},
                    {"description", desc}
                });
            }

            // User-installed modules (from ~/.vivid/modules/)
            output["user_installed"] = json::array();
            fs::path userModulesDir = fs::path(getenv("HOME") ? getenv("HOME") : "") / ".vivid" / "modules";
            if (fs::exists(userModulesDir) && fs::is_directory(userModulesDir)) {
                for (const auto& entry : fs::directory_iterator(userModulesDir)) {
                    if (entry.is_directory()) {
                        fs::path moduleJson = entry.path() / "module.json";
                        if (fs::exists(moduleJson)) {
                            try {
                                std::ifstream f(moduleJson);
                                json meta = json::parse(f);
                                output["user_installed"].push_back({
                                    {"name", meta.value("name", entry.path().filename().string())},
                                    {"description", meta.value("description", "")},
                                    {"version", meta.value("version", "")}
                                });
                            } catch (...) {
                                // Malformed module.json, skip
                            }
                        }
                    }
                }
            }

            result["content"] = {{{"type", "text"}, {"text", output.dump(2)}}};
        }
        else if (name == "list_templates") {
            // Return hardcoded template info (matches CLI new command)
            json templates = json::array();
            templates.push_back({
                {"name", "blank"},
                {"description", "Empty project with minimal setup - just outputs a solid color"}
            });
            templates.push_back({
                {"name", "minimal"},
                {"description", "Minimal setup() and update() functions with no operators"}
            });
            templates.push_back({
                {"name", "noise-demo"},
                {"description", "Animated fractal noise generator - good starting point for visuals"}
            });
            templates.push_back({
                {"name", "feedback"},
                {"description", "Feedback loop with zoom/rotate - creates tunnel and trail effects"}
            });
            templates.push_back({
                {"name", "audio-visualizer"},
                {"description", "FFT audio analysis driving visual parameters (requires vivid-audio)"}
            });
            templates.push_back({
                {"name", "3d-orbit"},
                {"description", "Orbiting 3D camera around a sphere (requires vivid-render3d)"}
            });

            result["content"] = {{{"type", "text"}, {"text", templates.dump(2)}}};
        }
        else if (name == "search_docs") {
            std::string query = args.value("query", "");
            if (query.empty()) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Query is required"}}};
                return result;
            }

            auto matches = vivid::docs::searchDocs(query);
            if (matches.empty()) {
                result["content"] = {{{"type", "text"}, {"text", "No matches found for '" + query + "'"}}};
            } else {
                result["content"] = {{{"type", "text"}, {"text", matches.dump(2)}}};
            }
        }
        else if (name == "get_performance_stats") {
            json stats = m_vivid.getPerformanceStats();
            if (stats.empty()) {
                json response;
                response["connected"] = m_vivid.isConnected();
                response["warning"] = "No performance data available yet. Vivid sends stats periodically.";
                response["suggestion"] = "Wait a moment and try again, or check if Vivid is running.";
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
            } else {
                return makeToolResult(stats, true);
            }
        }
        else if (name == "solo_operator") {
            std::string opName = args.value("name", "");
            if (opName.empty()) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Operator name is required"}}};
                return result;
            }

            auto connState = m_vivid.getConnectionState();
            if (!connState.connected) {
                json response;
                response["success"] = false;
                response["connected"] = false;
                response["error"] = "Cannot solo: Vivid not running";
                response["suggestion"] = "Run: ./build/bin/vivid <project>";
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                return result;
            }

            m_vivid.soloOperator(opName);
            json response;
            response["success"] = true;
            response["connected"] = true;
            response["message"] = "Soloed operator: " + opName;
            response["hint"] = "Use exit_solo to return to normal output";
            result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
        }
        else if (name == "exit_solo") {
            auto connState = m_vivid.getConnectionState();
            if (!connState.connected) {
                json response;
                response["success"] = false;
                response["connected"] = false;
                response["error"] = "Cannot exit solo: Vivid not running";
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                return result;
            }

            m_vivid.exitSolo();
            json response;
            response["success"] = true;
            response["connected"] = true;
            response["message"] = "Exited solo mode";
            result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
        }
        else if (name == "get_solo_state") {
            json state = m_vivid.getSoloState();
            return makeToolResult(state, true);
        }
        else if (name == "get_window_state") {
            json state = m_vivid.getWindowState();
            if (state.empty()) {
                json response;
                response["connected"] = m_vivid.isConnected();
                response["warning"] = "No window state available yet.";
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
            } else {
                return makeToolResult(state, true);
            }
        }
        else if (name == "set_window_mode") {
            std::string setting = args.value("setting", "");
            bool value = args.value("value", false);

            if (setting.empty()) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Setting is required (fullscreen, borderless, alwaysOnTop, cursor)"}}};
                return result;
            }

            auto connState = m_vivid.getConnectionState();
            if (!connState.connected) {
                json response;
                response["success"] = false;
                response["connected"] = false;
                response["error"] = "Cannot set window mode: Vivid not running";
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                return result;
            }

            m_vivid.setWindowControl(setting, value ? 1 : 0);
            json response;
            response["success"] = true;
            response["connected"] = true;
            response["message"] = "Set " + setting + " = " + (value ? "true" : "false");
            result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
        }
        else if (name == "capture_frame") {
            std::string outputPath = args.value("outputPath", "/tmp/vivid_capture.png");

            auto connState = m_vivid.getConnectionState();
            if (!connState.connected) {
                json response;
                response["success"] = false;
                response["connected"] = false;
                response["error"] = "Cannot capture frame: Vivid not running";
                response["suggestion"] = "Run: ./build/bin/vivid <project>";
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                return result;
            }

            // Send capture command and wait for result
            json captureResult = m_vivid.captureFrame(outputPath);

            json response;
            response["connected"] = true;
            response["success"] = captureResult.value("success", false);
            response["outputPath"] = captureResult.value("outputPath", outputPath);
            if (captureResult.contains("error")) {
                response["error"] = captureResult["error"];
            }
            if (response["success"].get<bool>()) {
                response["hint"] = "Use the Read tool to view the captured image";
            }
            result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
        }
        else if (name == "set_param") {
            std::string opName = args.value("operator", "");
            std::string paramName = args.value("param", "");

            if (opName.empty() || paramName.empty()) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Both 'operator' and 'param' are required"}}};
                return result;
            }

            if (!args.contains("value")) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "'value' is required"}}};
                return result;
            }

            auto connState = m_vivid.getConnectionState();
            if (!connState.connected) {
                json response;
                response["success"] = false;
                response["connected"] = false;
                response["error"] = "Cannot set param: Vivid not running";
                response["suggestion"] = "Run: ./build/bin/vivid <project>";
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                return result;
            }

            // Send set_param_immediate and wait for result
            json setResult = m_vivid.setParamImmediate(opName, paramName, args["value"]);

            json response;
            response["connected"] = true;
            response["success"] = setResult.value("success", false);
            response["operator"] = opName;
            response["param"] = paramName;
            if (setResult.contains("error")) {
                response["error"] = setResult["error"];
            }
            result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
        }
        else if (name == "advance_frames") {
            int count = args.value("count", 1);

            if (count <= 0) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Frame count must be positive"}}};
                return result;
            }

            auto connState = m_vivid.getConnectionState();
            if (!connState.connected) {
                json response;
                response["success"] = false;
                response["connected"] = false;
                response["error"] = "Cannot advance frames: Vivid not running";
                response["suggestion"] = "Run: ./build/bin/vivid <project>";
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                return result;
            }

            // Send advance_frames and wait for completion
            json advResult = m_vivid.advanceFrames(count);

            json response;
            response["connected"] = true;
            response["success"] = advResult.value("success", !advResult.contains("error"));
            response["framesAdvanced"] = count;
            if (advResult.contains("newFrame")) {
                response["newFrame"] = advResult["newFrame"];
            }
            if (advResult.contains("error")) {
                response["error"] = advResult["error"];
            }
            result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
        }
        else if (name == "orbit_camera") {
            std::string camOp = args.value("operator", "camera");

            if (!args.contains("target") || !args.contains("distance")) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Both 'target' and 'distance' are required"}}};
                return result;
            }

            auto connState = m_vivid.getConnectionState();
            if (!connState.connected) {
                json response;
                response["success"] = false;
                response["connected"] = false;
                response["error"] = "Cannot orbit camera: Vivid not running";
                response["suggestion"] = "Run: ./build/bin/vivid <project>";
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                return result;
            }

            // Extract parameters
            auto target = args["target"];
            float distance = args["distance"].get<float>();
            float azimuth = args.value("azimuth", 0.0f);
            float elevation = args.value("elevation", 20.0f);

            // Convert degrees to radians for azimuth/elevation
            float azimuthRad = azimuth * 3.14159265f / 180.0f;
            float elevationRad = elevation * 3.14159265f / 180.0f;

            // Set camera parameters
            bool allSuccess = true;

            // Set center (target)
            json centerResult = m_vivid.setParamImmediate(camOp, "center", target);
            if (!centerResult.value("success", false)) allSuccess = false;

            // Set distance
            json distResult = m_vivid.setParamImmediate(camOp, "distance", distance);
            if (!distResult.value("success", false)) allSuccess = false;

            // Set azimuth
            json azResult = m_vivid.setParamImmediate(camOp, "azimuth", azimuthRad);
            if (!azResult.value("success", false)) allSuccess = false;

            // Set elevation
            json elResult = m_vivid.setParamImmediate(camOp, "elevation", elevationRad);
            if (!elResult.value("success", false)) allSuccess = false;

            json response;
            response["connected"] = true;
            response["success"] = allSuccess;
            response["operator"] = camOp;
            response["target"] = target;
            response["distance"] = distance;
            response["azimuth"] = azimuth;
            response["elevation"] = elevation;
            if (!allSuccess) {
                response["warning"] = "Some parameters may not have been set. Check if operator '" + camOp + "' exists and has expected parameters.";
            }
            result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
        }
        else if (name == "capture_at_frame") {
            int targetFrame = args.value("frame", 0);
            std::string outputPath = args.value("outputPath", "/tmp/vivid_capture.png");

            auto connState = m_vivid.getConnectionState();
            if (!connState.connected) {
                json response;
                response["success"] = false;
                response["connected"] = false;
                response["error"] = "Cannot capture at frame: Vivid not running";
                response["suggestion"] = "Run: ./build/bin/vivid <project>";
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                return result;
            }

            // First, advance to the target frame if needed
            if (targetFrame > 0) {
                json advResult = m_vivid.advanceFrames(targetFrame);
                if (advResult.contains("error")) {
                    json response;
                    response["connected"] = true;
                    response["success"] = false;
                    response["error"] = "Failed to advance frames: " + advResult.value("error", "unknown");
                    result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                    return result;
                }
            }

            // Then capture the frame
            json captureResult = m_vivid.captureFrame(outputPath);

            json response;
            response["connected"] = true;
            response["success"] = captureResult.value("success", false);
            response["frame"] = targetFrame;
            response["outputPath"] = captureResult.value("outputPath", outputPath);
            if (captureResult.contains("error")) {
                response["error"] = captureResult["error"];
            }
            if (response["success"].get<bool>()) {
                response["hint"] = "Use the Read tool to view the captured image";
            }
            result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
        }
        else if (name == "sweep_param") {
            std::string op = args.value("operator", "");
            std::string param = args.value("param", "");

            if (op.empty() || param.empty() || !args.contains("from") || !args.contains("to")) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Required: operator, param, from, to"}}};
                return result;
            }

            auto connState = m_vivid.getConnectionState();
            if (!connState.connected) {
                json response;
                response["success"] = false;
                response["connected"] = false;
                response["error"] = "Cannot sweep parameter: Vivid not running";
                response["suggestion"] = "Run: ./build/bin/vivid <project>";
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                return result;
            }

            double from = args["from"].get<double>();
            double to = args["to"].get<double>();
            int steps = std::clamp(args.value("steps", 5), 2, 20);
            int settleFrames = std::max(0, args.value("settle_frames", 3));
            std::string outputDir = args.value("output_dir", "/tmp/vivid_sweep");

            // Create output directory
            try {
                fs::create_directories(outputDir);
            } catch (const std::exception& e) {
                json response;
                response["connected"] = true;
                response["success"] = false;
                response["error"] = std::string("Failed to create output directory: ") + e.what();
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                return result;
            }

            // Read current param value so we can restore it
            json originalValue;
            bool foundOriginal = false;
            {
                json params = m_vivid.getParams();
                for (const auto& p : params) {
                    if (p.value("operator", "") == op && p.value("name", "") == param) {
                        originalValue = p["value"];
                        foundOriginal = true;
                        break;
                    }
                }
            }

            // Sweep loop
            json captures = json::array();
            for (int i = 0; i < steps; ++i) {
                double value = (steps == 1) ? from : from + (to - from) * i / (steps - 1);

                // Set parameter
                json setResult = m_vivid.setParamImmediate(op, param, value);
                if (!setResult.value("success", false)) {
                    captures.push_back({
                        {"step", i},
                        {"value", value},
                        {"error", setResult.value("error", "Failed to set parameter")}
                    });
                    continue;
                }

                // Let effects settle
                if (settleFrames > 0) {
                    m_vivid.advanceFrames(settleFrames);
                }

                // Build filename
                char buf[256];
                snprintf(buf, sizeof(buf), "sweep_%s_%s_%04d.png", op.c_str(), param.c_str(), i);
                fs::path filePath = fs::path(outputDir) / buf;

                // Capture frame
                json captureResult = m_vivid.captureFrame(filePath.string());
                if (captureResult.value("success", false)) {
                    captures.push_back({
                        {"step", i},
                        {"value", value},
                        {"path", filePath.string()}
                    });
                } else {
                    captures.push_back({
                        {"step", i},
                        {"value", value},
                        {"error", captureResult.value("error", "Capture failed")}
                    });
                }
            }

            // Restore original value
            bool restored = false;
            if (foundOriginal) {
                json restoreResult = m_vivid.setParamImmediate(op, param, originalValue);
                restored = restoreResult.value("success", false);
                if (restored && settleFrames > 0) {
                    m_vivid.advanceFrames(settleFrames);
                }
            }

            json response;
            response["connected"] = true;
            response["success"] = true;
            response["operator"] = op;
            response["param"] = param;
            response["from"] = from;
            response["to"] = to;
            response["steps"] = steps;
            response["settle_frames"] = settleFrames;
            response["captures"] = captures;
            if (foundOriginal) {
                response["original_value"] = originalValue;
            }
            response["restored"] = restored;
            result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
        }
        else if (name == "capture_audio") {
            std::string outputPath = args.value("outputPath", "/tmp/vivid_capture.wav");
            float duration = args.value("duration", 1.0f);
            duration = std::max(0.01f, std::min(30.0f, duration));

            auto connState = m_vivid.getConnectionState();
            if (!connState.connected) {
                json response;
                response["success"] = false;
                response["connected"] = false;
                response["error"] = "Cannot capture audio: Vivid not running";
                response["suggestion"] = "Run: ./build/bin/vivid <project>";
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                return result;
            }

            json captureResult = m_vivid.captureAudio(outputPath, duration);

            json response;
            response["connected"] = true;
            response["success"] = captureResult.value("success", false);
            response["outputPath"] = captureResult.value("outputPath", outputPath);
            response["duration"] = duration;
            if (captureResult.contains("analysis")) {
                response["analysis"] = captureResult["analysis"];
            }
            if (captureResult.contains("error")) {
                response["error"] = captureResult["error"];
            }
            result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
        }
        else if (name == "sweep_param_audio") {
            std::string op = args.value("operator", "");
            std::string param = args.value("param", "");

            if (op.empty() || param.empty() || !args.contains("from") || !args.contains("to")) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Required: operator, param, from, to"}}};
                return result;
            }

            auto connState = m_vivid.getConnectionState();
            if (!connState.connected) {
                json response;
                response["success"] = false;
                response["connected"] = false;
                response["error"] = "Cannot sweep parameter audio: Vivid not running";
                response["suggestion"] = "Run: ./build/bin/vivid <project>";
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                return result;
            }

            double from = args["from"].get<double>();
            double to = args["to"].get<double>();
            int steps = std::clamp(args.value("steps", 5), 2, 10);
            float audioDuration = std::clamp(args.value("audio_duration", 0.5f), 0.1f, 5.0f);
            int settleFrames = std::max(0, args.value("settle_frames", 3));
            std::string outputDir = args.value("output_dir", "/tmp/vivid_sweep_audio");

            // Create output directory
            try {
                fs::create_directories(outputDir);
            } catch (const std::exception& e) {
                json response;
                response["connected"] = true;
                response["success"] = false;
                response["error"] = std::string("Failed to create output directory: ") + e.what();
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                return result;
            }

            // Read current param value so we can restore it
            json originalValue;
            bool foundOriginal = false;
            {
                json params = m_vivid.getParams();
                for (const auto& p : params) {
                    if (p.value("operator", "") == op && p.value("name", "") == param) {
                        originalValue = p["value"];
                        foundOriginal = true;
                        break;
                    }
                }
            }

            // Sweep loop
            json captures = json::array();
            for (int i = 0; i < steps; ++i) {
                double value = (steps == 1) ? from : from + (to - from) * i / (steps - 1);

                // Set parameter
                json setResult = m_vivid.setParamImmediate(op, param, value);
                if (!setResult.value("success", false)) {
                    captures.push_back({
                        {"step", i},
                        {"value", value},
                        {"error", setResult.value("error", "Failed to set parameter")}
                    });
                    continue;
                }

                // Let audio settle
                if (settleFrames > 0) {
                    m_vivid.advanceFrames(settleFrames);
                }

                // Build filename
                char buf[256];
                snprintf(buf, sizeof(buf), "sweep_%s_%s_%04d.wav", op.c_str(), param.c_str(), i);
                fs::path filePath = fs::path(outputDir) / buf;

                // Capture audio using existing captureAudio (blocks until complete)
                json captureResult = m_vivid.captureAudio(filePath.string(), audioDuration);

                json entry;
                entry["step"] = i;
                entry["value"] = value;
                if (captureResult.value("success", false)) {
                    entry["wav_path"] = filePath.string();
                    if (captureResult.contains("analysis")) {
                        entry["analysis"] = captureResult["analysis"];
                    }
                } else {
                    entry["error"] = captureResult.value("error", "Audio capture failed");
                }
                captures.push_back(entry);
            }

            // Restore original value
            bool restored = false;
            if (foundOriginal) {
                json restoreResult = m_vivid.setParamImmediate(op, param, originalValue);
                restored = restoreResult.value("success", false);
                if (restored && settleFrames > 0) {
                    m_vivid.advanceFrames(settleFrames);
                }
            }

            json response;
            response["connected"] = true;
            response["success"] = true;
            response["operator"] = op;
            response["param"] = param;
            response["from"] = from;
            response["to"] = to;
            response["steps"] = steps;
            response["audio_duration"] = audioDuration;
            response["settle_frames"] = settleFrames;
            response["captures"] = captures;
            if (foundOriginal) {
                response["original_value"] = originalValue;
            }
            response["restored"] = restored;
            result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
        }
        else if (name == "compare_frames") {
            std::string imageA = args.value("image_a", "");
            std::string imageB = args.value("image_b", "");

            if (imageA.empty() || imageB.empty()) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Required: image_a, image_b"}}};
                return result;
            }

            int threshold = std::clamp(args.value("threshold", 5), 0, 255);

            // Load both images
            auto imgA = vivid::io::loadImage(imageA);
            if (!imgA.valid()) {
                json response;
                response["success"] = false;
                response["error"] = "Failed to load image_a: " + imageA;
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                return result;
            }

            auto imgB = vivid::io::loadImage(imageB);
            if (!imgB.valid()) {
                json response;
                response["success"] = false;
                response["error"] = "Failed to load image_b: " + imageB;
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                return result;
            }

            // Check dimensions match
            if (imgA.width != imgB.width || imgA.height != imgB.height) {
                json response;
                response["success"] = false;
                response["error"] = "Image dimensions don't match";
                response["image_a"] = {{"width", imgA.width}, {"height", imgA.height}};
                response["image_b"] = {{"width", imgB.width}, {"height", imgB.height}};
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                return result;
            }

            int totalPixels = imgA.width * imgA.height;
            double sumSqR = 0, sumSqG = 0, sumSqB = 0;
            double sumAbsR = 0, sumAbsG = 0, sumAbsB = 0;
            int changedCount = 0;
            bool identical = true;

            for (int i = 0; i < totalPixels; ++i) {
                int idx = i * 4; // RGBA
                int dR = static_cast<int>(imgA.pixels[idx])     - static_cast<int>(imgB.pixels[idx]);
                int dG = static_cast<int>(imgA.pixels[idx + 1]) - static_cast<int>(imgB.pixels[idx + 1]);
                int dB = static_cast<int>(imgA.pixels[idx + 2]) - static_cast<int>(imgB.pixels[idx + 2]);

                if (dR != 0 || dG != 0 || dB != 0) {
                    identical = false;
                }

                // Normalized to 0-1
                double nR = dR / 255.0, nG = dG / 255.0, nB = dB / 255.0;

                sumSqR += nR * nR;
                sumSqG += nG * nG;
                sumSqB += nB * nB;

                sumAbsR += std::abs(nR);
                sumAbsG += std::abs(nG);
                sumAbsB += std::abs(nB);

                if (std::abs(dR) > threshold || std::abs(dG) > threshold || std::abs(dB) > threshold) {
                    ++changedCount;
                }
            }

            double rmse = std::sqrt((sumSqR + sumSqG + sumSqB) / (3.0 * totalPixels));
            double percentage = (totalPixels > 0) ? (100.0 * changedCount / totalPixels) : 0.0;

            json response;
            response["success"] = true;
            response["image_a"] = imageA;
            response["image_b"] = imageB;
            response["resolution"] = {{"width", imgA.width}, {"height", imgA.height}};
            response["rmse"] = std::round(rmse * 10000.0) / 10000.0; // 4 decimal places
            response["mean_diff"] = {
                {"r", std::round(sumAbsR / totalPixels * 10000.0) / 10000.0},
                {"g", std::round(sumAbsG / totalPixels * 10000.0) / 10000.0},
                {"b", std::round(sumAbsB / totalPixels * 10000.0) / 10000.0}
            };
            response["changed_pixels"] = {
                {"count", changedCount},
                {"total", totalPixels},
                {"percentage", std::round(percentage * 100.0) / 100.0}, // 2 decimal places
                {"threshold", threshold}
            };
            response["identical"] = identical;
            result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
        }
        else if (name == "compare_audio") {
            std::string audioA = args.value("audio_a", "");
            std::string audioB = args.value("audio_b", "");

            if (audioA.empty() || audioB.empty()) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Both 'audio_a' and 'audio_b' paths are required"}}};
                return result;
            }

            // Read both WAV files
            std::vector<float> samplesA, samplesB;
            uint32_t framesA, framesB, chA, chB, srA, srB;

            if (!vivid::readWAV(audioA, samplesA, framesA, chA, srA)) {
                json response;
                response["success"] = false;
                response["error"] = "Failed to read audio file: " + audioA;
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                return result;
            }

            if (!vivid::readWAV(audioB, samplesB, framesB, chB, srB)) {
                json response;
                response["success"] = false;
                response["error"] = "Failed to read audio file: " + audioB;
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                return result;
            }

            // Analyze each
            auto analysisA = vivid::analyzeAudioBuffer(samplesA.data(), framesA, chA, srA);
            auto analysisB = vivid::analyzeAudioBuffer(samplesB.data(), framesB, chB, srB);

            // Compute diff metrics
            float rmsDiff = std::abs(analysisA.rmsLevel - analysisB.rmsLevel);
            float peakDiff = std::abs(analysisA.peakLevel - analysisB.peakLevel);

            // Per-band spectral diff
            json spectrumDiff = json::object();
            const char* bandNames[] = {"subBass", "bass", "lowMid", "mid", "highMid", "high"};
            for (int i = 0; i < 6; i++) {
                spectrumDiff[bandNames[i]] = std::round(
                    std::abs(analysisA.spectrum[i] - analysisB.spectrum[i]) * 10000.0f) / 10000.0f;
            }

            // Correlation coefficient (on overlapping frames)
            uint32_t minFrames = std::min(framesA, framesB);
            uint32_t minCh = std::min(chA, chB);
            uint32_t overlapSamples = minFrames * minCh;
            double correlation = 0.0;

            if (overlapSamples > 0) {
                double meanA = 0, meanB = 0;
                for (uint32_t i = 0; i < overlapSamples; i++) {
                    meanA += samplesA[i];
                    meanB += samplesB[i];
                }
                meanA /= overlapSamples;
                meanB /= overlapSamples;

                double sumAB = 0, sumAA = 0, sumBB = 0;
                for (uint32_t i = 0; i < overlapSamples; i++) {
                    double a = samplesA[i] - meanA;
                    double b = samplesB[i] - meanB;
                    sumAB += a * b;
                    sumAA += a * a;
                    sumBB += b * b;
                }

                double denom = std::sqrt(sumAA * sumBB);
                correlation = denom > 0 ? sumAB / denom : 0.0;
            }

            json response;
            response["success"] = true;
            response["audio_a"] = audioA;
            response["audio_b"] = audioB;
            response["analysis_a"] = json::parse(analysisA.toJSON());
            response["analysis_b"] = json::parse(analysisB.toJSON());
            response["diff"] = {
                {"rmsDiff", std::round(rmsDiff * 10000.0f) / 10000.0f},
                {"peakDiff", std::round(peakDiff * 10000.0f) / 10000.0f},
                {"spectrumDiff", spectrumDiff},
                {"correlation", std::round(correlation * 10000.0) / 10000.0}
            };
            result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
        }
        else if (name == "list_project_assets") {
            std::string projectPath = args.value("path", "");

            if (projectPath.empty()) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Project path is required"}}};
                return result;
            }

            fs::path assetsDir = fs::path(projectPath) / "assets";
            if (!fs::exists(assetsDir) || !fs::is_directory(assetsDir)) {
                json response;
                response["path"] = projectPath;
                response["assets"] = json::array();
                response["note"] = "No assets/ directory found in project";
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                return result;
            }

            // Scan assets directory recursively
            json assets = json::array();
            try {
                for (const auto& entry : fs::recursive_directory_iterator(assetsDir)) {
                    if (!entry.is_regular_file()) continue;

                    json asset;
                    fs::path relPath = fs::relative(entry.path(), assetsDir);
                    asset["path"] = relPath.string();

                    // Determine asset type from extension
                    std::string ext = entry.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

                    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif" || ext == ".bmp" || ext == ".tga") {
                        asset["type"] = "image";
                    } else if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac" || ext == ".aiff") {
                        asset["type"] = "audio";
                    } else if (ext == ".gltf" || ext == ".glb" || ext == ".obj" || ext == ".fbx") {
                        asset["type"] = "model";
                    } else if (ext == ".mp4" || ext == ".mov" || ext == ".avi" || ext == ".webm") {
                        asset["type"] = "video";
                    } else if (ext == ".ttf" || ext == ".otf" || ext == ".woff" || ext == ".woff2") {
                        asset["type"] = "font";
                    } else if (ext == ".json" || ext == ".xml" || ext == ".csv" || ext == ".txt") {
                        asset["type"] = "data";
                    } else if (ext == ".wgsl" || ext == ".glsl" || ext == ".frag" || ext == ".vert") {
                        asset["type"] = "shader";
                    } else {
                        asset["type"] = "other";
                    }

                    // Get file size
                    asset["size"] = entry.file_size();

                    assets.push_back(asset);
                }
            } catch (const std::exception& e) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", std::string("Error scanning assets: ") + e.what()}}};
                return result;
            }

            json response;
            response["path"] = projectPath;
            response["assetsDir"] = assetsDir.string();
            response["assets"] = assets;
            response["count"] = assets.size();
            result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
        }
        else if (name == "get_chain_structure") {
            auto connState = m_vivid.getConnectionState();
            if (!connState.connected) {
                json response;
                response["success"] = false;
                response["connected"] = false;
                response["error"] = "Cannot get chain structure: Vivid not running";
                response["suggestion"] = "Run: ./build/bin/vivid <project>";
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                return result;
            }

            // Request chain structure and wait for response
            json structure = m_vivid.requestChainStructure();

            json response;
            response["connected"] = true;
            response["success"] = !structure.contains("error");
            if (structure.contains("operators")) {
                response["operators"] = structure["operators"];
                response["operatorCount"] = structure["operators"].size();
            }
            if (structure.contains("error")) {
                response["error"] = structure["error"];
            }
            result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
        }
        else if (name == "inspect_chain") {
            auto connState = m_vivid.getConnectionState();
            if (!connState.connected) {
                json response;
                response["success"] = false;
                response["connected"] = false;
                response["error"] = "Cannot inspect chain: Vivid not running";
                response["suggestion"] = "Run: ./build/bin/vivid <project>";
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                return result;
            }

            // Request chain inspection and wait for response (longer timeout for GPU readback)
            bool perOp = args.value("per_operator_analysis", false);
            json inspection = m_vivid.requestInspectChain(perOp);

            json response;
            response["connected"] = true;
            if (inspection.contains("error")) {
                response["success"] = false;
                response["error"] = inspection["error"];
            } else if (inspection.contains("data")) {
                response["success"] = true;
                response["inspection"] = inspection["data"];
            } else {
                response["success"] = true;
                response["inspection"] = inspection;
            }
            result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
        }
        else if (name == "wait_for_reload") {
            int timeoutMs = args.value("timeout", 10000);

            auto connState = m_vivid.getConnectionState();
            if (!connState.connected) {
                json response;
                response["success"] = false;
                response["connected"] = false;
                response["error"] = "Cannot wait for reload: Vivid not running";
                response["suggestion"] = "Run: ./build/bin/vivid <project>";
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                return result;
            }

            // Wait for next compile_status message
            json compileStatus = m_vivid.waitForCompileStatus(timeoutMs);

            json response;
            response["connected"] = true;
            response["success"] = compileStatus.value("success", true);
            response["timeout"] = compileStatus.value("timeout", false);
            if (!compileStatus.value("success", true)) {
                response["error"] = compileStatus.value("message", "Compilation failed");
            }
            result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
        }
        else if (name == "get_frame_info") {
            auto connState = m_vivid.getConnectionState();
            if (!connState.connected) {
                json response;
                response["success"] = false;
                response["connected"] = false;
                response["error"] = "Cannot get frame info: Vivid not running";
                response["suggestion"] = "Run: ./build/bin/vivid <project>";
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                return result;
            }

            // Request frame info and wait for response
            json frameInfo = m_vivid.requestFrameInfo();

            json response;
            response["connected"] = true;
            response["success"] = !frameInfo.contains("error");
            if (frameInfo.contains("frame")) {
                response["frame"] = frameInfo["frame"];
                response["time"] = frameInfo["time"];
                response["fps"] = frameInfo["fps"];
            }
            if (frameInfo.contains("error")) {
                response["error"] = frameInfo["error"];
            }
            result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
        }
        else if (name == "reset_time") {
            auto connState = m_vivid.getConnectionState();
            if (!connState.connected) {
                json response;
                response["success"] = false;
                response["connected"] = false;
                response["error"] = "Cannot reset time: Vivid not running";
                response["suggestion"] = "Run: ./build/bin/vivid <project>";
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                return result;
            }

            // Send reset_time command and wait for acknowledgment
            json resetResult = m_vivid.resetTime();

            json response;
            response["connected"] = true;
            response["success"] = resetResult.value("success", true);
            response["message"] = "Animation reset to frame 0";
            result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
        }
        else if (name == "save_preset") {
            std::string projectPath = args.value("path", "");
            std::string presetName = args.value("name", "");

            if (projectPath.empty() || presetName.empty()) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Both 'path' and 'name' are required"}}};
                return result;
            }

            auto connState = m_vivid.getConnectionState();
            if (!connState.connected) {
                json response;
                response["success"] = false;
                response["connected"] = false;
                response["error"] = "Cannot save preset: Vivid not running (need live params)";
                response["suggestion"] = "Run: ./build/bin/vivid <project>";
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                return result;
            }

            // Get current params
            json params = m_vivid.getParams();

            // Create presets directory if needed
            fs::path presetsDir = fs::path(projectPath) / "presets";
            try {
                fs::create_directories(presetsDir);
            } catch (const std::exception& e) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", std::string("Cannot create presets directory: ") + e.what()}}};
                return result;
            }

            // Save preset
            fs::path presetPath = presetsDir / (presetName + ".json");
            try {
                json preset;
                preset["name"] = presetName;
                preset["params"] = params;
                preset["savedAt"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();

                std::ofstream file(presetPath);
                file << preset.dump(2);
            } catch (const std::exception& e) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", std::string("Failed to save preset: ") + e.what()}}};
                return result;
            }

            json response;
            response["success"] = true;
            response["connected"] = true;
            response["path"] = presetPath.string();
            response["paramCount"] = params.size();
            result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
        }
        else if (name == "load_preset") {
            std::string projectPath = args.value("path", "");
            std::string presetName = args.value("name", "");

            if (projectPath.empty() || presetName.empty()) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Both 'path' and 'name' are required"}}};
                return result;
            }

            auto connState = m_vivid.getConnectionState();
            if (!connState.connected) {
                json response;
                response["success"] = false;
                response["connected"] = false;
                response["error"] = "Cannot load preset: Vivid not running";
                response["suggestion"] = "Run: ./build/bin/vivid <project>";
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                return result;
            }

            // Load preset file
            fs::path presetPath = fs::path(projectPath) / "presets" / (presetName + ".json");
            if (!fs::exists(presetPath)) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Preset not found: " + presetPath.string()}}};
                return result;
            }

            json preset;
            try {
                std::ifstream file(presetPath);
                preset = json::parse(file);
            } catch (const std::exception& e) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", std::string("Failed to parse preset: ") + e.what()}}};
                return result;
            }

            // Apply each parameter
            int successCount = 0;
            int failCount = 0;
            for (const auto& param : preset["params"]) {
                std::string opName = param.value("operator", "");
                std::string paramName = param.value("name", "");
                if (opName.empty() || paramName.empty()) continue;

                // Build value array from param
                json value;
                if (param.contains("value")) {
                    value = param["value"];
                } else {
                    continue;  // Skip params without values
                }

                json setResult = m_vivid.setParamImmediate(opName, paramName, value);
                if (setResult.value("success", false)) {
                    successCount++;
                } else {
                    failCount++;
                }
            }

            json response;
            response["success"] = true;
            response["connected"] = true;
            response["path"] = presetPath.string();
            response["paramsApplied"] = successCount;
            response["paramsFailed"] = failCount;
            result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
        }
        else if (name == "list_snapshots") {
            std::string projectPath = args.value("path", "");
            if (projectPath.empty()) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "'path' is required"}}};
                return result;
            }

            fs::path snapshotPath = fs::path(projectPath) / "vivid-snapshots.json";
            json response;
            response["path"] = snapshotPath.string();

            if (!fs::exists(snapshotPath)) {
                response["snapshots"] = json::array();
                response["count"] = 0;
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                return result;
            }

            try {
                std::ifstream file(snapshotPath);
                json data = json::parse(file);
                json snapList = json::array();
                if (data.contains("snapshots") && data["snapshots"].is_array()) {
                    int idx = 0;
                    for (const auto& snap : data["snapshots"]) {
                        json entry;
                        entry["index"] = idx;
                        entry["name"] = snap.value("name", "Untitled");
                        // Count params
                        int paramCount = 0;
                        if (snap.contains("values") && snap["values"].is_object()) {
                            for (auto& [opName, opParams] : snap["values"].items()) {
                                if (opParams.is_object()) {
                                    paramCount += static_cast<int>(opParams.size());
                                }
                            }
                        }
                        entry["paramCount"] = paramCount;
                        snapList.push_back(entry);
                        idx++;
                    }
                }
                response["snapshots"] = snapList;
                response["count"] = snapList.size();
            } catch (const std::exception& e) {
                response["error"] = std::string("Failed to read snapshots: ") + e.what();
                response["snapshots"] = json::array();
                response["count"] = 0;
            }

            result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
        }
        else if (name == "save_snapshot") {
            std::string projectPath = args.value("path", "");
            std::string snapName = args.value("name", "");

            if (projectPath.empty() || snapName.empty()) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Both 'path' and 'name' are required"}}};
                return result;
            }

            auto connState = m_vivid.getConnectionState();
            if (!connState.connected) {
                json response;
                response["success"] = false;
                response["connected"] = false;
                response["error"] = "Cannot save snapshot: Vivid not running (need live params)";
                response["suggestion"] = "Run: ./build/bin/vivid <project>";
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                return result;
            }

            // Get current params from running instance
            json params = m_vivid.getParams();

            // Convert flat param list to snapshot format: {opName: {paramName: [v0,v1,v2,v3]}}
            json snapValues = json::object();
            for (const auto& param : params) {
                std::string opName = param.value("operator", "");
                std::string paramName = param.value("name", "");
                if (opName.empty() || paramName.empty()) continue;
                if (!param.contains("value")) continue;

                // Normalize value to float[4]
                json val = param["value"];
                json arr = json::array();
                if (val.is_array()) {
                    for (size_t i = 0; i < 4; i++) {
                        arr.push_back(i < val.size() ? val[i].get<float>() : 0.0f);
                    }
                } else if (val.is_number()) {
                    arr = {val.get<float>(), 0.0f, 0.0f, 0.0f};
                } else {
                    arr = {0.0f, 0.0f, 0.0f, 0.0f};
                }

                if (!snapValues.contains(opName)) {
                    snapValues[opName] = json::object();
                }
                snapValues[opName][paramName] = arr;
            }

            // Load existing file or create new
            fs::path snapshotPath = fs::path(projectPath) / "vivid-snapshots.json";
            json data;
            if (fs::exists(snapshotPath)) {
                try {
                    std::ifstream file(snapshotPath);
                    data = json::parse(file);
                } catch (...) {
                    data = json::object();
                }
            }
            if (!data.contains("snapshots") || !data["snapshots"].is_array()) {
                data["snapshots"] = json::array();
            }

            // Add new snapshot
            json newSnap;
            newSnap["name"] = snapName;
            newSnap["values"] = snapValues;
            data["snapshots"].push_back(newSnap);

            // Save
            try {
                std::ofstream file(snapshotPath);
                file << data.dump(2);
            } catch (const std::exception& e) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", std::string("Failed to save: ") + e.what()}}};
                return result;
            }

            json response;
            response["success"] = true;
            response["connected"] = true;
            response["name"] = snapName;
            response["index"] = static_cast<int>(data["snapshots"].size()) - 1;
            response["path"] = snapshotPath.string();
            result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
        }
        else if (name == "recall_snapshot") {
            std::string projectPath = args.value("path", "");
            std::string snapName = args.value("name", "");
            float crossfade = args.value("crossfade", 0.0f);

            if (projectPath.empty() || snapName.empty()) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Both 'path' and 'name' are required"}}};
                return result;
            }

            auto connState = m_vivid.getConnectionState();
            if (!connState.connected) {
                json response;
                response["success"] = false;
                response["connected"] = false;
                response["error"] = "Cannot recall snapshot: Vivid not running";
                response["suggestion"] = "Run: ./build/bin/vivid <project>";
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                return result;
            }

            // Load snapshots file
            fs::path snapshotPath = fs::path(projectPath) / "vivid-snapshots.json";
            if (!fs::exists(snapshotPath)) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "No snapshots file found: " + snapshotPath.string()}}};
                return result;
            }

            json data;
            try {
                std::ifstream file(snapshotPath);
                data = json::parse(file);
            } catch (const std::exception& e) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", std::string("Failed to parse: ") + e.what()}}};
                return result;
            }

            if (!data.contains("snapshots") || !data["snapshots"].is_array()) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Invalid snapshots file"}}};
                return result;
            }

            // Find snapshot by name or index
            int snapIdx = -1;
            // Try as index first
            try {
                int asInt = std::stoi(snapName);
                if (asInt >= 0 && asInt < static_cast<int>(data["snapshots"].size())) {
                    snapIdx = asInt;
                }
            } catch (...) {}

            // Try by name
            if (snapIdx < 0) {
                for (size_t i = 0; i < data["snapshots"].size(); i++) {
                    if (data["snapshots"][i].value("name", "") == snapName) {
                        snapIdx = static_cast<int>(i);
                        break;
                    }
                }
            }

            if (snapIdx < 0) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Snapshot not found: " + snapName}}};
                return result;
            }

            const auto& snap = data["snapshots"][snapIdx];

            // Apply params (crossfade is not supported via MCP — always hard cut)
            // Crossfade requires the in-process SnapshotStore ticking each frame
            int successCount = 0;
            int failCount = 0;

            if (snap.contains("values") && snap["values"].is_object()) {
                for (auto& [opName, opParams] : snap["values"].items()) {
                    if (!opParams.is_object()) continue;
                    for (auto& [paramName, val] : opParams.items()) {
                        json setResult = m_vivid.setParamImmediate(opName, paramName, val);
                        if (setResult.value("success", false)) {
                            successCount++;
                        } else {
                            failCount++;
                        }
                    }
                }
            }

            json response;
            response["success"] = true;
            response["connected"] = true;
            response["name"] = snap.value("name", "");
            response["index"] = snapIdx;
            response["paramsApplied"] = successCount;
            response["paramsFailed"] = failCount;
            if (crossfade > 0.0f) {
                response["note"] = "Crossfade is only available in-app (via PresetPanel). MCP applies hard cut.";
            }
            result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
        }
        else if (name == "delete_snapshot") {
            std::string projectPath = args.value("path", "");
            std::string snapName = args.value("name", "");

            if (projectPath.empty() || snapName.empty()) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Both 'path' and 'name' are required"}}};
                return result;
            }

            fs::path snapshotPath = fs::path(projectPath) / "vivid-snapshots.json";
            if (!fs::exists(snapshotPath)) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "No snapshots file found"}}};
                return result;
            }

            json data;
            try {
                std::ifstream file(snapshotPath);
                data = json::parse(file);
            } catch (const std::exception& e) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", std::string("Failed to parse: ") + e.what()}}};
                return result;
            }

            if (!data.contains("snapshots") || !data["snapshots"].is_array()) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Invalid snapshots file"}}};
                return result;
            }

            // Find snapshot by name or index
            int snapIdx = -1;
            try {
                int asInt = std::stoi(snapName);
                if (asInt >= 0 && asInt < static_cast<int>(data["snapshots"].size())) {
                    snapIdx = asInt;
                }
            } catch (...) {}

            if (snapIdx < 0) {
                for (size_t i = 0; i < data["snapshots"].size(); i++) {
                    if (data["snapshots"][i].value("name", "") == snapName) {
                        snapIdx = static_cast<int>(i);
                        break;
                    }
                }
            }

            if (snapIdx < 0) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Snapshot not found: " + snapName}}};
                return result;
            }

            std::string deletedName = data["snapshots"][snapIdx].value("name", "");
            data["snapshots"].erase(data["snapshots"].begin() + snapIdx);

            // Save updated file
            try {
                std::ofstream file(snapshotPath);
                file << data.dump(2);
            } catch (const std::exception& e) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", std::string("Failed to save: ") + e.what()}}};
                return result;
            }

            json response;
            response["success"] = true;
            response["deleted"] = deletedName;
            response["remaining"] = data["snapshots"].size();
            result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
        }
        else if (name == "export_video") {
            std::string projectPath = args.value("path", "");
            std::string outputPath = args.value("output", "");
            float duration = args.value("duration", 0.0f);
            std::string script = args.value("script", "");
            float fps = args.value("fps", 0.0f);
            std::string codec = args.value("codec", "");
            bool audio = args.value("audio", false);
            std::string resolution = args.value("resolution", "");

            if (projectPath.empty() || outputPath.empty() || duration <= 0.0f) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "path, output, and duration (> 0) are required"}}};
                return result;
            }

            std::vector<std::string> cmdArgs = {
                getVividExecutable(), "export", projectPath,
                "-o", outputPath,
                "--duration", std::to_string(duration),
                "--quiet"
            };

            if (!script.empty()) {
                cmdArgs.push_back("--script");
                cmdArgs.push_back(script);
            }
            if (fps > 0.0f) {
                cmdArgs.push_back("--fps");
                cmdArgs.push_back(std::to_string(fps));
            }
            if (!codec.empty()) {
                cmdArgs.push_back("--codec");
                cmdArgs.push_back(codec);
            }
            if (audio) {
                cmdArgs.push_back("--audio");
            }
            if (!resolution.empty()) {
                cmdArgs.push_back("--resolution");
                cmdArgs.push_back(resolution);
            }

            auto cmdResult = runCommand(cmdArgs, 600000);  // 10 minute timeout

            if (cmdResult.exitCode == 0) {
                json response;
                response["success"] = true;
                response["output"] = outputPath;

                // Report file size
                try {
                    if (fs::exists(outputPath)) {
                        auto fileSize = fs::file_size(outputPath);
                        response["fileSizeBytes"] = fileSize;
                        // Human-readable size
                        if (fileSize >= 1024 * 1024) {
                            response["fileSize"] = std::to_string(fileSize / (1024 * 1024)) + " MB";
                        } else if (fileSize >= 1024) {
                            response["fileSize"] = std::to_string(fileSize / 1024) + " KB";
                        } else {
                            response["fileSize"] = std::to_string(fileSize) + " bytes";
                        }
                    }
                } catch (...) {}

                // Parse warnings from output
                json warnings = json::array();
                std::istringstream stream(cmdResult.output);
                std::string line;
                while (std::getline(stream, line)) {
                    if (line.find("warning") != std::string::npos ||
                        line.find("Warning") != std::string::npos) {
                        warnings.push_back(line);
                    }
                }
                if (!warnings.empty()) {
                    response["warnings"] = warnings;
                }

                if (!cmdResult.output.empty()) {
                    response["log"] = cmdResult.output;
                }
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
            } else {
                // Try to parse structured compile errors
                json errors = parseCompileErrors(cmdResult.output);
                json response;
                response["success"] = false;
                if (!errors.empty()) {
                    response["compileErrors"] = errors;
                    response["errorCount"] = errors.size();
                }
                response["raw"] = cmdResult.output;
                response["exitCode"] = cmdResult.exitCode;
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
            }
        }
        else if (name == "run_project") {
            std::string projectPath = args.value("path", "");
            bool showUI = args.value("show_ui", true);
            std::string resolution = args.value("resolution", "");

            if (projectPath.empty()) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "path is required"}}};
                return result;
            }

            // Check if already connected or a child is already running
            if (m_vivid.isConnected()) {
                json response;
                response["success"] = false;
                response["error"] = "A Vivid instance is already connected on port 9876";
                response["suggestion"] = "Use stop_project first, or use the existing connection";
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                return result;
            }

#ifndef _WIN32
            if (m_childPid > 0) {
                // Check if the child is still alive
                int status;
                pid_t ret = waitpid(m_childPid, &status, WNOHANG);
                if (ret == 0) {
                    // Still running
                    json response;
                    response["success"] = false;
                    response["error"] = "A Vivid instance is already running (PID " + std::to_string(m_childPid) + ")";
                    response["suggestion"] = "Use stop_project first";
                    result["isError"] = true;
                    result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                    return result;
                }
                // Child exited, clean up
                m_childPid = 0;
                m_childProjectPath.clear();
            }
#else
            if (m_childProcess != NULL) {
                DWORD exitCode;
                if (GetExitCodeProcess(m_childProcess, &exitCode) && exitCode == STILL_ACTIVE) {
                    json response;
                    response["success"] = false;
                    response["error"] = "A Vivid instance is already running (PID " + std::to_string(m_childPid) + ")";
                    response["suggestion"] = "Use stop_project first";
                    result["isError"] = true;
                    result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                    return result;
                }
                CloseHandle(m_childProcess);
                m_childProcess = NULL;
                m_childPid = 0;
                m_childProjectPath.clear();
            }
#endif

            std::string executable = getVividExecutable();

#ifndef _WIN32
            // Build argv for execv
            std::vector<std::string> argStrings;
            argStrings.push_back(executable);
            argStrings.push_back(projectPath);
            if (showUI) {
                argStrings.push_back("--show-ui");
            }
            if (!resolution.empty()) {
                // Parse resolution into --width and --height if needed
                // For now pass as-is; the CLI might accept --resolution
            }

            pid_t pid = fork();
            if (pid < 0) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Failed to fork process"}}};
                return result;
            }

            if (pid == 0) {
                // Child process — redirect stdout/stderr to /dev/null
                // to prevent corrupting MCP stdio
                int devNull = open("/dev/null", O_WRONLY);
                if (devNull >= 0) {
                    dup2(devNull, STDOUT_FILENO);
                    dup2(devNull, STDERR_FILENO);
                    close(devNull);
                }

                // Build C-style argv
                std::vector<char*> argv;
                for (auto& s : argStrings) {
                    argv.push_back(const_cast<char*>(s.c_str()));
                }
                argv.push_back(nullptr);

                execv(executable.c_str(), argv.data());
                // If execv returns, it failed
                _exit(127);
            }

            // Parent process
            m_childPid = pid;
            m_childProjectPath = projectPath;
            std::cerr << "[MCP] Spawned Vivid child process PID " << pid << "\n";
#else
            // Windows: CreateProcess
            std::string cmdLine = "\"" + executable + "\" \"" + projectPath + "\"";
            if (showUI) {
                cmdLine += " --show-ui";
            }

            STARTUPINFOA si = {};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESTDHANDLES;
            // Redirect child stdout/stderr to NUL
            HANDLE hNull = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
            si.hStdOutput = hNull;
            si.hStdError = hNull;
            si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

            PROCESS_INFORMATION pi = {};
            BOOL created = CreateProcessA(NULL, const_cast<char*>(cmdLine.c_str()),
                NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);

            if (hNull != INVALID_HANDLE_VALUE) CloseHandle(hNull);

            if (!created) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Failed to create process"}}};
                return result;
            }

            CloseHandle(pi.hThread);
            m_childProcess = pi.hProcess;
            m_childPid = pi.dwProcessId;
            m_childProjectPath = projectPath;
            std::cerr << "[MCP] Spawned Vivid child process PID " << m_childPid << "\n";
#endif

            // Wait for the process to start up and WebSocket to become available
            // Check periodically for early crash
            std::cerr << "[MCP] Waiting for Vivid to start...\n";
            for (int i = 0; i < 30; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

#ifndef _WIN32
                int status;
                pid_t ret = waitpid(m_childPid, &status, WNOHANG);
                if (ret > 0) {
                    // Child exited early — crashed or failed
                    m_childPid = 0;
                    m_childProjectPath.clear();
                    json response;
                    response["success"] = false;
                    response["error"] = "Vivid process exited immediately (status " + std::to_string(WEXITSTATUS(status)) + ")";
                    response["suggestion"] = "Check project path and chain.cpp for errors. Use validate_chain to debug.";
                    result["isError"] = true;
                    result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                    return result;
                }
#else
                DWORD exitCode;
                if (GetExitCodeProcess(m_childProcess, &exitCode) && exitCode != STILL_ACTIVE) {
                    CloseHandle(m_childProcess);
                    m_childProcess = NULL;
                    m_childPid = 0;
                    m_childProjectPath.clear();
                    json response;
                    response["success"] = false;
                    response["error"] = "Vivid process exited immediately (code " + std::to_string(exitCode) + ")";
                    response["suggestion"] = "Check project path and chain.cpp for errors. Use validate_chain to debug.";
                    result["isError"] = true;
                    result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                    return result;
                }
#endif
            }

            // Now try to connect via WebSocket
            std::cerr << "[MCP] Attempting WebSocket connection...\n";
            bool connected = m_vivid.connect();

            json response;
            response["success"] = true;
            response["connected"] = connected;
            response["pid"] = static_cast<int>(m_childPid);
            response["project"] = projectPath;
            if (!connected) {
                response["warning"] = "Process started but WebSocket not yet ready. Live tools may not work immediately — try get_runtime_status in a few seconds.";
            }
            result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
        }
        else if (name == "stop_project") {
#ifndef _WIN32
            if (m_childPid <= 0) {
#else
            if (m_childProcess == NULL) {
#endif
                // No child process — but maybe we should disconnect anyway
                if (m_vivid.isConnected()) {
                    m_vivid.disconnect();
                    json response;
                    response["success"] = true;
                    response["message"] = "Disconnected from Vivid (no child process to stop — was it started externally?)";
                    result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                } else {
                    json response;
                    response["success"] = false;
                    response["error"] = "No Vivid instance is running";
                    result["isError"] = true;
                    result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                }
                return result;
            }

            // Disconnect WebSocket first to prevent error callbacks
            m_vivid.disconnect();

            bool graceful = false;

#ifndef _WIN32
            // Send SIGTERM for graceful shutdown
            std::cerr << "[MCP] Sending SIGTERM to PID " << m_childPid << "\n";
            kill(m_childPid, SIGTERM);

            // Wait up to 3 seconds for graceful exit
            for (int i = 0; i < 30; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                int status;
                pid_t ret = waitpid(m_childPid, &status, WNOHANG);
                if (ret > 0) {
                    graceful = true;
                    break;
                }
            }

            if (!graceful) {
                // Force kill
                std::cerr << "[MCP] SIGTERM timeout, sending SIGKILL to PID " << m_childPid << "\n";
                kill(m_childPid, SIGKILL);
                int status;
                waitpid(m_childPid, &status, 0);  // Reap
            }

            m_childPid = 0;
#else
            // Windows: try graceful termination, then force
            std::cerr << "[MCP] Terminating PID " << m_childPid << "\n";

            // Try WM_CLOSE first
            // (TerminateProcess is the fallback)
            DWORD waitResult = WaitForSingleObject(m_childProcess, 0);
            if (waitResult != WAIT_OBJECT_0) {
                // Still running — try gentle termination
                TerminateProcess(m_childProcess, 0);
                waitResult = WaitForSingleObject(m_childProcess, 3000);
                graceful = (waitResult == WAIT_OBJECT_0);
                if (!graceful) {
                    TerminateProcess(m_childProcess, 1);
                    WaitForSingleObject(m_childProcess, 1000);
                }
            } else {
                graceful = true;
            }

            CloseHandle(m_childProcess);
            m_childProcess = NULL;
            m_childPid = 0;
#endif
            std::string projectPath = m_childProjectPath;
            m_childProjectPath.clear();

            json response;
            response["success"] = true;
            response["graceful"] = graceful;
            response["project"] = projectPath;
            if (graceful) {
                response["message"] = "Vivid stopped gracefully";
            } else {
                response["message"] = "Vivid was force-killed after timeout";
            }
            result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
        }
        else {
            result["isError"] = true;
            result["content"] = {{
                {"type", "text"},
                {"text", "Unknown tool: " + name}
            }};
        }

        return result;
    }

    // Delegate to shared docs utility
    json handleResourcesList() {
        json resources = json::array();

        for (const auto& [filename, name] : vivid::docs::getDocFiles()) {
            std::string uriPath = filename.substr(0, filename.length() - 3);
            std::transform(uriPath.begin(), uriPath.end(), uriPath.begin(), ::tolower);

            resources.push_back({
                {"uri", "vivid://docs/" + uriPath},
                {"name", name},
                {"description", "Vivid documentation: " + name},
                {"mimeType", "text/markdown"}
            });
        }

        return {{"resources", resources}};
    }

    json handleResourcesRead(const json& params) {
        std::string uri = params.value("uri", "");
        json result;

        const std::string prefix = "vivid://docs/";
        if (uri.find(prefix) == 0) {
            std::string uriPath = uri.substr(prefix.length());
            std::transform(uriPath.begin(), uriPath.end(), uriPath.begin(), ::toupper);
            std::string filename = uriPath + ".md";

            std::string content = vivid::docs::loadDocFile(filename);
            if (!content.empty()) {
                result["contents"] = {{
                    {"uri", uri},
                    {"mimeType", "text/markdown"},
                    {"text", content}
                }};
                return result;
            }
        }

        result["contents"] = json::array();
        return result;
    }

    VividConnection m_vivid;

    // Child process tracking for run_project/stop_project
#ifndef _WIN32
    pid_t m_childPid = 0;
#else
    HANDLE m_childProcess = NULL;
    DWORD m_childPid = 0;
#endif
    std::string m_childProjectPath;
};

int runServer() {
    // Load all modules (built-in + user-installed) to populate OperatorRegistry
    vivid::loadAllModules();

    McpServer server;
    return server.run();
}

} // namespace vivid::mcp
