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
            } else if (type == "frame_info") {
                m_frameInfo = msg;
                m_frameInfoReceived = true;
            } else if (type == "reset_time_complete") {
                m_resetTimeResult = msg;
                m_resetTimeReceived = true;
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
            {"description", "Capture the current frame from a running Vivid instance to a PNG file. Unlike capture_snapshot which spawns a new process, this captures from the live running instance immediately. Use this to verify visual output after making changes."},
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
            status["operators"] = m_vivid.getOperators();
            status["pendingChanges"] = m_vivid.getPendingChanges()["hasChanges"];

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
            bool snippetOnly = args.value("snippet_only", false);
            if (opName.empty()) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Operator name is required"}}};
                return result;
            }

            json examples = json::array();

            // Search RECIPES.md for code blocks containing the operator
            std::string recipes = loadDocsFile("RECIPES.md");
            if (!recipes.empty()) {
                // Find code blocks that contain the operator name
                size_t pos = 0;
                while ((pos = recipes.find("```cpp", pos)) != std::string::npos) {
                    size_t codeStart = pos + 6;  // After ```cpp
                    size_t codeEnd = recipes.find("```", codeStart);
                    if (codeEnd == std::string::npos) break;

                    std::string codeBlock = recipes.substr(codeStart, codeEnd - codeStart);

                    // Check if this code block uses the operator
                    // Look for patterns like: add<Operator>, chain.add<Operator>, Operator(
                    std::string pattern1 = "add<" + opName + ">";
                    std::string pattern2 = opName + "(";
                    if (codeBlock.find(pattern1) != std::string::npos ||
                        codeBlock.find(pattern2) != std::string::npos) {

                        std::string outputCode;

                        if (snippetOnly) {
                            // Extract just the relevant lines (operator + next few lines)
                            std::istringstream stream(codeBlock);
                            std::string line;
                            std::string snippet;
                            bool capturing = false;
                            int captureLines = 0;

                            while (std::getline(stream, line)) {
                                if (!capturing) {
                                    if (line.find(pattern1) != std::string::npos ||
                                        line.find(pattern2) != std::string::npos) {
                                        capturing = true;
                                    }
                                }
                                if (capturing) {
                                    snippet += line + "\n";
                                    captureLines++;
                                    // Capture up to 10 lines or until we hit a blank line after first few
                                    if (captureLines > 10 || (captureLines > 3 && line.empty())) {
                                        break;
                                    }
                                }
                            }
                            outputCode = snippet;
                        } else {
                            // Return the FULL code block for complete context
                            outputCode = codeBlock;
                        }

                        if (!outputCode.empty()) {
                            // Trim leading/trailing whitespace
                            while (!outputCode.empty() && (outputCode.front() == '\n' || outputCode.front() == ' ')) {
                                outputCode.erase(0, 1);
                            }
                            while (!outputCode.empty() && (outputCode.back() == '\n' || outputCode.back() == ' ')) {
                                outputCode.pop_back();
                            }

                            // Try to find the recipe title (look for ## heading before this code block)
                            std::string recipeTitle;
                            size_t searchStart = (pos > 500) ? pos - 500 : 0;
                            std::string beforeCode = recipes.substr(searchStart, pos - searchStart);
                            size_t headingPos = beforeCode.rfind("\n## ");
                            if (headingPos != std::string::npos) {
                                size_t titleStart = headingPos + 4;
                                size_t titleEnd = beforeCode.find('\n', titleStart);
                                if (titleEnd != std::string::npos) {
                                    recipeTitle = beforeCode.substr(titleStart, titleEnd - titleStart);
                                }
                            }

                            json example = {
                                {"source", "docs/RECIPES.md"},
                                {"code", outputCode}
                            };
                            if (!recipeTitle.empty()) {
                                example["recipe"] = recipeTitle;
                            }
                            examples.push_back(example);
                        }
                    }
                    pos = codeEnd + 3;
                }
            }

            // Search example chain.cpp files - try multiple locations
            std::vector<fs::path> moduleSearchPaths;
            moduleSearchPaths.push_back(fs::current_path() / "modules");

            fs::path exeDir = fs::path(getVividExecutable()).parent_path();
            if (!exeDir.empty()) {
                // build/bin/vivid -> modules (go up 2 levels)
                moduleSearchPaths.push_back(exeDir.parent_path().parent_path() / "modules");
            }

            for (const auto& modulesPath : moduleSearchPaths) {
                if (!fs::exists(modulesPath)) continue;

                for (const auto& moduleDir : fs::directory_iterator(modulesPath)) {
                    if (!fs::is_directory(moduleDir)) continue;
                    fs::path examplesPath = moduleDir.path() / "examples";
                    if (!fs::exists(examplesPath)) continue;

                    for (const auto& exampleDir : fs::directory_iterator(examplesPath)) {
                        if (!fs::is_directory(exampleDir)) continue;
                        fs::path chainFile = exampleDir.path() / "chain.cpp";
                        if (!fs::exists(chainFile)) continue;

                        std::ifstream file(chainFile);
                        if (!file.is_open()) continue;

                        std::string content((std::istreambuf_iterator<char>(file)),
                                           std::istreambuf_iterator<char>());

                        // Check if this file uses the operator
                        std::string pattern1 = "add<" + opName + ">";
                        if (content.find(pattern1) == std::string::npos) continue;

                        std::string outputCode;

                        if (snippetOnly) {
                            // Extract relevant lines
                            std::istringstream stream(content);
                            std::string line;
                            std::string snippet;
                            bool capturing = false;
                            int captureLines = 0;

                            while (std::getline(stream, line)) {
                                if (!capturing) {
                                    if (line.find(pattern1) != std::string::npos) {
                                        capturing = true;
                                    }
                                }
                                if (capturing) {
                                    snippet += line + "\n";
                                    captureLines++;
                                    // Capture up to 12 lines or until we hit a function/block end
                                    if (captureLines > 12 ||
                                        (captureLines > 3 && (line.find("}") == 0 || line.empty()))) {
                                        break;
                                    }
                                }
                            }
                            outputCode = snippet;
                        } else {
                            // Return the FULL chain.cpp file for complete context
                            outputCode = content;
                        }

                        if (!outputCode.empty()) {
                            while (!outputCode.empty() && (outputCode.back() == '\n' || outputCode.back() == ' ')) {
                                outputCode.pop_back();
                            }
                            // Get relative path for cleaner output
                            std::string relPath = fs::relative(chainFile, modulesPath.parent_path()).string();
                            examples.push_back({
                                {"source", relPath},
                                {"code", outputCode}
                            });
                        }

                        // Limit examples per operator
                        if (examples.size() >= 3) break;
                    }
                    if (examples.size() >= 3) break;
                }
                if (examples.size() >= 3) break;
            }

            if (examples.empty()) {
                json response;
                response["operator"] = opName;
                response["examples"] = json::array();
                response["message"] = "No examples found for '" + opName + "'. Try list_operators to verify the name.";
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
            } else {
                json response;
                response["operator"] = opName;
                response["examples"] = examples;
                response["count"] = examples.size();
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
            }
        }
        else if (name == "get_recipe") {
            std::string recipeName = args.value("name", "");
            std::string recipes = loadDocsFile("RECIPES.md");

            if (recipes.empty()) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Could not load RECIPES.md"}}};
                return result;
            }

            // Parse all recipes: find ## headings and their code blocks
            struct Recipe {
                std::string name;
                std::string code;
                std::string description;
            };
            std::vector<Recipe> allRecipes;

            size_t pos = 0;
            while ((pos = recipes.find("\n## ", pos)) != std::string::npos) {
                // Extract recipe name
                size_t titleStart = pos + 4;
                size_t titleEnd = recipes.find('\n', titleStart);
                if (titleEnd == std::string::npos) break;

                std::string title = recipes.substr(titleStart, titleEnd - titleStart);

                // Find the next ## or end of file to get recipe section
                size_t nextSection = recipes.find("\n## ", titleEnd);
                if (nextSection == std::string::npos) nextSection = recipes.length();

                std::string section = recipes.substr(titleEnd, nextSection - titleEnd);

                // Find code block in this section
                size_t codeStart = section.find("```cpp");
                if (codeStart != std::string::npos) {
                    codeStart += 6;  // After ```cpp
                    size_t codeEnd = section.find("```", codeStart);
                    if (codeEnd != std::string::npos) {
                        std::string code = section.substr(codeStart, codeEnd - codeStart);
                        // Trim leading/trailing whitespace
                        while (!code.empty() && (code.front() == '\n' || code.front() == ' ')) {
                            code.erase(0, 1);
                        }
                        while (!code.empty() && (code.back() == '\n' || code.back() == ' ')) {
                            code.pop_back();
                        }

                        // Extract description (text between title and code block)
                        std::string desc;
                        size_t descEnd = section.find("```cpp");
                        if (descEnd != std::string::npos && descEnd > 0) {
                            desc = section.substr(0, descEnd);
                            // Trim and clean up
                            while (!desc.empty() && (desc.front() == '\n' || desc.front() == ' ')) {
                                desc.erase(0, 1);
                            }
                            while (!desc.empty() && (desc.back() == '\n' || desc.back() == ' ')) {
                                desc.pop_back();
                            }
                            // Limit description length
                            if (desc.length() > 200) {
                                desc = desc.substr(0, 197) + "...";
                            }
                        }

                        allRecipes.push_back({title, code, desc});
                    }
                }
                pos = titleEnd;
            }

            if (recipeName.empty()) {
                // List all recipes
                json response;
                response["count"] = allRecipes.size();
                response["recipes"] = json::array();
                response["hint"] = "Use get_recipe with a name to get the full code. Start with a simple recipe and modify incrementally!";
                for (const auto& r : allRecipes) {
                    json recipe;
                    recipe["name"] = r.name;
                    if (!r.description.empty()) {
                        recipe["description"] = r.description;
                    }
                    response["recipes"].push_back(recipe);
                }
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
            } else {
                // Find recipe by name (case-insensitive partial match)
                std::string searchLower = recipeName;
                std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);

                const Recipe* match = nullptr;
                for (const auto& r : allRecipes) {
                    std::string nameLower = r.name;
                    std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
                    if (nameLower.find(searchLower) != std::string::npos) {
                        match = &r;
                        break;
                    }
                }

                if (match) {
                    json response;
                    response["name"] = match->name;
                    response["code"] = match->code;
                    if (!match->description.empty()) {
                        response["description"] = match->description;
                    }
                    response["hint"] = "This is a complete, working example. After modifying, use validate_chain to check for errors!";
                    result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                } else {
                    // No match - suggest similar names
                    json response;
                    response["error"] = "Recipe '" + recipeName + "' not found";
                    response["available"] = json::array();
                    for (const auto& r : allRecipes) {
                        response["available"].push_back(r.name);
                    }
                    result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
                }
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

            // Split query into words (lowercase, strip punctuation)
            std::vector<std::string> queryWords;
            {
                std::string queryLower = query;
                std::transform(queryLower.begin(), queryLower.end(), queryLower.begin(), ::tolower);
                std::istringstream iss(queryLower);
                std::string word;
                while (iss >> word) {
                    // Strip punctuation from word
                    word.erase(std::remove_if(word.begin(), word.end(),
                        [](char c) { return !std::isalnum(static_cast<unsigned char>(c)); }), word.end());
                    if (!word.empty()) {
                        queryWords.push_back(word);
                    }
                }
            }

            if (queryWords.empty()) {
                result["content"] = {{{"type", "text"}, {"text", "No valid search terms in query"}}};
                return result;
            }

            // Dynamically discover all doc files
            auto docs = getDocFiles();
            json matches = json::array();

            for (const auto& [filename, title] : docs) {
                std::string content = loadDocsFile(filename);
                if (content.empty()) continue;

                // Convert content to lowercase for searching
                std::string contentLower = content;
                std::transform(contentLower.begin(), contentLower.end(), contentLower.begin(), ::tolower);

                // Split into sections by markdown headers
                std::vector<std::tuple<std::string, size_t, size_t>> sections;  // header, start, end
                size_t pos = 0;
                std::string currentHeader = title;
                size_t currentStart = 0;

                while ((pos = content.find("\n#", pos)) != std::string::npos) {
                    // Save previous section
                    if (pos > currentStart) {
                        sections.emplace_back(currentHeader, currentStart, pos);
                    }
                    // Extract new header
                    size_t headerEnd = content.find('\n', pos + 1);
                    if (headerEnd != std::string::npos) {
                        std::string header = content.substr(pos + 1, headerEnd - pos - 1);
                        // Clean up # characters
                        size_t hashEnd = header.find_first_not_of("# ");
                        if (hashEnd != std::string::npos) {
                            currentHeader = title + " > " + header.substr(hashEnd);
                        } else {
                            currentHeader = title + " > " + header;
                        }
                    }
                    currentStart = pos + 1;
                    pos++;
                }
                // Add final section
                sections.emplace_back(currentHeader, currentStart, content.length());

                // Search each section - use OR logic with relevance scoring
                for (const auto& [section, sectionStart, sectionEnd] : sections) {
                    std::string sectionContent = contentLower.substr(sectionStart, sectionEnd - sectionStart);

                    // Count how many query words appear in this section (OR logic)
                    int matchScore = 0;
                    size_t firstWordPos = std::string::npos;
                    for (const auto& word : queryWords) {
                        size_t wordPos = sectionContent.find(word);
                        if (wordPos != std::string::npos) {
                            matchScore++;
                            if (firstWordPos == std::string::npos || wordPos < firstWordPos) {
                                firstWordPos = wordPos;
                            }
                        }
                    }

                    // Include if ANY word matches (OR logic)
                    if (matchScore > 0 && firstWordPos != std::string::npos) {
                        // Extract context around first word (in original case)
                        size_t contextStart = (firstWordPos > 150) ? firstWordPos - 150 : 0;
                        size_t contextLen = std::min(size_t(400), sectionEnd - sectionStart - contextStart);

                        std::string context = content.substr(sectionStart + contextStart, contextLen);
                        // Trim to word boundaries
                        if (contextStart > 0) {
                            size_t firstSpace = context.find(' ');
                            if (firstSpace != std::string::npos) context = "..." + context.substr(firstSpace);
                        }
                        if (contextStart + contextLen < sectionEnd - sectionStart) {
                            size_t lastSpace = context.rfind(' ');
                            if (lastSpace != std::string::npos) context = context.substr(0, lastSpace) + "...";
                        }

                        matches.push_back({
                            {"file", filename},
                            {"section", section},
                            {"context", context},
                            {"score", matchScore}
                        });
                    }
                }
            }

            if (matches.empty()) {
                result["content"] = {{{"type", "text"}, {"text", "No matches found for '" + query + "'"}}};
            } else {
                // Sort by score (highest first), then limit to 10
                std::sort(matches.begin(), matches.end(), [](const json& a, const json& b) {
                    return a["score"].get<int>() > b["score"].get<int>();
                });
                if (matches.size() > 10) {
                    matches = json::array_t(matches.begin(), matches.begin() + 10);
                }
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
        else {
            result["isError"] = true;
            result["content"] = {{
                {"type", "text"},
                {"text", "Unknown tool: " + name}
            }};
        }

        return result;
    }

    // Find the docs directory
    fs::path findDocsDir() {
        std::vector<fs::path> searchPaths;

        // 1. Current working directory
        searchPaths.push_back(fs::current_path() / "docs");

        // 2. Relative to executable
        fs::path exeDir = fs::path(getVividExecutable()).parent_path();
        searchPaths.push_back(exeDir.parent_path().parent_path() / "docs");
        searchPaths.push_back(exeDir.parent_path() / "docs");

        // 3. User's .vivid directory
        const char* home = getenv("HOME");
#ifdef _WIN32
        if (!home) home = getenv("USERPROFILE");
#endif
        if (home) {
            searchPaths.push_back(fs::path(home) / ".vivid" / "docs");
        }

        for (const auto& path : searchPaths) {
            if (fs::exists(path) && fs::is_directory(path)) {
                return path;
            }
        }
        return {};
    }

    // Find the modules directory (built-in modules)
    fs::path findModulesDir() {
        std::vector<fs::path> searchPaths;

        // 1. Current working directory (dev: running from repo root)
        searchPaths.push_back(fs::current_path() / "modules");

        // 2. Relative to executable
        fs::path exeDir = fs::path(getVividExecutable()).parent_path();
        // Release: exe in bin/, modules at ../modules/
        searchPaths.push_back(exeDir.parent_path() / "modules");
        // Dev: exe in build/bin/, modules at ../../modules/
        searchPaths.push_back(exeDir.parent_path().parent_path() / "modules");

        for (const auto& path : searchPaths) {
            if (fs::exists(path) && fs::is_directory(path)) {
                return path;
            }
        }
        return {};
    }

    // Get user's home directory
    std::string getHomeDir() {
        const char* home = getenv("HOME");
#ifdef _WIN32
        if (!home) home = getenv("USERPROFILE");
#endif
        return home ? home : "";
    }

    // Format module name: "vivid-audio" -> "Audio"
    std::string formatModuleName(const std::string& dirName) {
        std::string name = dirName;
        // Remove "vivid-" prefix
        if (name.rfind("vivid-", 0) == 0) {
            name = name.substr(6);
        }
        // Capitalize first letter
        if (!name.empty()) {
            name[0] = std::toupper(static_cast<unsigned char>(name[0]));
        }
        return name;
    }

    // Format example name: "drum-machine" -> "Drum Machine"
    std::string formatExampleName(const std::string& dirName) {
        std::string name = dirName;
        std::replace(name.begin(), name.end(), '-', ' ');
        std::replace(name.begin(), name.end(), '_', ' ');
        // Capitalize first letter of each word
        bool capitalizeNext = true;
        for (char& c : name) {
            if (c == ' ') {
                capitalizeNext = true;
            } else if (capitalizeNext) {
                c = std::toupper(static_cast<unsigned char>(c));
                capitalizeNext = false;
            }
        }
        return name;
    }

    // Scan a modules directory for READMEs and example CLAUDE.md files
    void scanModulesDir(const fs::path& modulesDir, std::vector<std::pair<std::string, std::string>>& docs, bool isUserModules = false) {
        if (!fs::exists(modulesDir) || !fs::is_directory(modulesDir)) return;

        try {
            for (const auto& moduleEntry : fs::directory_iterator(modulesDir)) {
                if (!moduleEntry.is_directory()) continue;

                std::string moduleDirName = moduleEntry.path().filename().string();
                std::string moduleName = formatModuleName(moduleDirName);
                std::string prefix = isUserModules ? "[User] " : "";

                // Check for module README.md
                fs::path readme = moduleEntry.path() / "README.md";
                if (fs::exists(readme)) {
                    docs.push_back({readme.string(), prefix + moduleName + " Module"});
                }

                // Scan examples for AGENTS.md files (or legacy CLAUDE.md)
                fs::path examplesDir = moduleEntry.path() / "examples";
                if (fs::exists(examplesDir) && fs::is_directory(examplesDir)) {
                    for (const auto& exampleEntry : fs::directory_iterator(examplesDir)) {
                        if (!exampleEntry.is_directory()) continue;

                        // Prefer AGENTS.md (new standard), fall back to CLAUDE.md
                        fs::path agentsMd = exampleEntry.path() / "AGENTS.md";
                        fs::path claudeMd = exampleEntry.path() / "CLAUDE.md";
                        fs::path docFile = fs::exists(agentsMd) ? agentsMd : claudeMd;
                        if (fs::exists(docFile)) {
                            std::string exampleName = formatExampleName(exampleEntry.path().filename().string());
                            docs.push_back({docFile.string(), prefix + moduleName + ": " + exampleName});
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            // Ignore errors (permission issues, etc.)
        }
    }

    // Get all markdown files in docs directory and modules
    std::vector<std::pair<std::string, std::string>> getDocFiles() {
        std::vector<std::pair<std::string, std::string>> docs;

        // 1. Scan docs/ directory
        fs::path docsDir = findDocsDir();
        if (!docsDir.empty()) {
            for (const auto& entry : fs::directory_iterator(docsDir)) {
                if (entry.is_regular_file() && entry.path().extension() == ".md") {
                    std::string filename = entry.path().filename().string();
                    // Skip README.md (usually just an index)
                    if (filename == "README.md") continue;

                    // Create human-readable name from filename
                    std::string name = filename.substr(0, filename.length() - 3);  // Remove .md
                    // Replace - and _ with spaces, capitalize words
                    std::replace(name.begin(), name.end(), '-', ' ');
                    std::replace(name.begin(), name.end(), '_', ' ');

                    docs.push_back({entry.path().string(), name});
                }
            }
        }

        // 2. Scan built-in modules (modules/vivid-*/README.md and examples/*/AGENTS.md)
        fs::path modulesDir = findModulesDir();
        scanModulesDir(modulesDir, docs, false);

        // 3. Scan user-installed modules (~/.vivid/modules/*)
        std::string homeDir = getHomeDir();
        if (!homeDir.empty()) {
            fs::path userModulesDir = fs::path(homeDir) / ".vivid" / "modules";
            scanModulesDir(userModulesDir, docs, true);
        }

        return docs;
    }

    json handleResourcesList() {
        json resources = json::array();

        // Dynamically discover all .md files in docs/
        for (const auto& [filename, name] : getDocFiles()) {
            // Create URI from filename (lowercase, no extension)
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

        // Extract filename from URI (vivid://docs/chain-api -> CHAIN-API.md)
        const std::string prefix = "vivid://docs/";
        if (uri.find(prefix) == 0) {
            std::string uriPath = uri.substr(prefix.length());
            // Convert to uppercase and add .md
            std::transform(uriPath.begin(), uriPath.end(), uriPath.begin(), ::toupper);
            std::string filename = uriPath + ".md";

            std::string content = loadDocsFile(filename);
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

    std::string loadDocsFile(const std::string& filename) {
        // First, check if filename is already a full/absolute path that exists
        // (used for module docs which are returned with full paths)
        fs::path directPath(filename);
        if (fs::exists(directPath)) {
            std::ifstream file(directPath);
            if (file) {
                std::stringstream buffer;
                buffer << file.rdbuf();
                return buffer.str();
            }
        }

        // Search multiple locations for documentation files (for docs/ files)
        std::vector<fs::path> searchPaths;

        // 1. Current working directory (common for dev builds)
        searchPaths.push_back(fs::current_path() / "docs" / filename);

        // 2. User home directory cache
        const char* home = getenv("HOME");
#ifdef _WIN32
        if (!home) home = getenv("USERPROFILE");
#endif
        if (home) {
            searchPaths.push_back(fs::path(home) / ".vivid" / "docs" / filename);
        }

        // 3. Paths relative to executable
        fs::path exeDir;
#ifdef __APPLE__
        char pathBuf[4096];
        uint32_t size = sizeof(pathBuf);
        if (_NSGetExecutablePath(pathBuf, &size) == 0) {
            exeDir = fs::path(pathBuf).parent_path();
        }
#elif defined(_WIN32)
        char pathBuf[MAX_PATH];
        if (GetModuleFileNameA(NULL, pathBuf, MAX_PATH) > 0) {
            exeDir = fs::path(pathBuf).parent_path();
        }
#else
        // Linux: read /proc/self/exe
        char pathBuf[4096];
        ssize_t len = readlink("/proc/self/exe", pathBuf, sizeof(pathBuf) - 1);
        if (len != -1) {
            pathBuf[len] = '\0';
            exeDir = fs::path(pathBuf).parent_path();
        }
#endif

        if (!exeDir.empty()) {
            // build/bin/vivid -> docs (go up 2 levels)
            searchPaths.push_back(exeDir.parent_path().parent_path() / "docs" / filename);
            // build/bin/vivid -> project root/docs (go up 3 levels for some layouts)
            searchPaths.push_back(exeDir.parent_path().parent_path().parent_path() / "docs" / filename);
            // Installed location: bin/../share/vivid/docs
            searchPaths.push_back(exeDir.parent_path() / "share" / "vivid" / "docs" / filename);
            // macOS app bundle: .app/Contents/MacOS/vivid -> .app/Contents/Resources/docs
            searchPaths.push_back(exeDir.parent_path() / "Resources" / "docs" / filename);
        }

        for (const auto& path : searchPaths) {
            if (fs::exists(path)) {
                std::ifstream file(path);
                if (file) {
                    std::stringstream buffer;
                    buffer << file.rdbuf();
                    return buffer.str();
                }
            }
        }

        return "Documentation file not found: " + filename;
    }

    VividConnection m_vivid;
};

int runServer() {
    // Load all modules (built-in + user-installed) to populate OperatorRegistry
    vivid::loadAllModules();

    McpServer server;
    return server.run();
}

} // namespace vivid::mcp
