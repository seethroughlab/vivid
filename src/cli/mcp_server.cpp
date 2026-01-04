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

#ifdef __APPLE__
#include <mach-o/dyld.h>
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

namespace vivid::mcp {

// WebSocket connection to running Vivid instance
class VividConnection {
public:
    VividConnection() = default;
    ~VividConnection() { disconnect(); }

    bool connect(int port = 9876) {
        std::string url = "ws://127.0.0.1:" + std::to_string(port);
        m_ws.setUrl(url);

        m_ws.setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
            if (msg->type == ix::WebSocketMessageType::Message) {
                handleMessage(msg->str);
            } else if (msg->type == ix::WebSocketMessageType::Open) {
                m_connected = true;
                // Request current state
                sendCommand("request_operators");
                sendCommand("request_pending_changes");
            } else if (msg->type == ix::WebSocketMessageType::Close) {
                m_connected = false;
            }
        });

        m_ws.start();

        // Wait for connection (up to 2 seconds)
        for (int i = 0; i < 20 && !m_connected; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Wait a bit longer for operator/param data to arrive
        if (m_connected) {
            for (int i = 0; i < 10; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                std::lock_guard<std::mutex> lock(m_mutex);
                if (!m_params.empty()) break;  // Got data
            }
        }

        return m_connected;
    }

    void disconnect() {
        m_ws.stop();
        m_connected = false;
    }

    bool isConnected() const { return m_connected; }

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

private:
    void handleMessage(const std::string& msgStr) {
        try {
            json msg = json::parse(msgStr);
            std::string type = msg.value("type", "");

            std::lock_guard<std::mutex> lock(m_mutex);

            if (type == "operator_list") {
                m_operators = msg["operators"];
            } else if (type == "param_values") {
                m_params = msg["params"];
            } else if (type == "pending_changes") {
                m_pendingChanges = msg;
            } else if (type == "compile_status") {
                m_compileStatus = msg;
            }
        } catch (...) {}
    }

    ix::WebSocket m_ws;
    std::atomic<bool> m_connected{false};
    mutable std::mutex m_mutex;

    // Cached state
    json m_operators = json::array();
    json m_params = json::array();
    json m_pendingChanges = {{"hasChanges", false}, {"changes", json::array()}};
    json m_compileStatus = {{"success", true}, {"message", ""}};
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

// Running vivid process handle (for run_project/stop_project)
#ifdef _WIN32
static HANDLE s_runningProcess = nullptr;
static DWORD s_runningPid = 0;
#else
static pid_t s_runningPid = 0;
#endif

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

        // list_operators - List available operators (from registry)
        tools.push_back({
            {"name", "list_operators"},
            {"description", "Get a list of all available Vivid operators with their parameters, grouped by category."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", json::object()}
            }}
        });

        // get_operator - Get details for specific operator
        tools.push_back({
            {"name", "get_operator"},
            {"description", "Get detailed information about a specific Vivid operator including parameters and usage."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"name", {{"type", "string"}, {"description", "Operator name (e.g., 'Noise', 'Blur', 'Feedback')"}}}
                }},
                {"required", json::array({"name"})}
            }}
        });

        // search_docs - Search documentation
        tools.push_back({
            {"name", "search_docs"},
            {"description", "Search Vivid documentation for relevant information about operators, patterns, or API details."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"query", {{"type", "string"}, {"description", "Search query"}}}
                }},
                {"required", json::array({"query"})}
            }}
        });

        // create_project - Create new project
        tools.push_back({
            {"name", "create_project"},
            {"description", "Create a new Vivid project with the specified name and template."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"name", {{"type", "string"}, {"description", "Project name"}}},
                    {"path", {{"type", "string"}, {"description", "Parent directory (optional, defaults to current directory)"}}},
                    {"template", {{"type", "string"}, {"description", "Template: blank, noise-demo, feedback, audio-visualizer, 3d-orbit"}}},
                    {"addons", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Addons to include: vivid-audio, vivid-video, vivid-render3d"}}},
                    {"force", {{"type", "boolean"}, {"description", "If true, remove existing directory first (use with caution)"}}}
                }},
                {"required", json::array({"name"})}
            }}
        });

        // run_project - Start a project
        tools.push_back({
            {"name", "run_project"},
            {"description", "Start a Vivid project in the background. Returns once the project is running."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"path", {{"type", "string"}, {"description", "Path to project directory"}}}
                }},
                {"required", json::array({"path"})}
            }}
        });

        // stop_project - Stop running project
        tools.push_back({
            {"name", "stop_project"},
            {"description", "Stop the currently running Vivid project."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", json::object()}
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
            {"description", "Check if a project's chain.cpp compiles without running it. Returns compilation errors if any."},
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

        // list_addons - List available addons
        tools.push_back({
            {"name", "list_addons"},
            {"description", "List installed Vivid addons."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", json::object()}
            }}
        });

        return {{"tools", tools}};
    }

    json handleToolsCall(const json& params) {
        std::string name = params.value("name", "");
        auto args = params.value("arguments", json::object());

        json result;
        result["isError"] = false;

        if (name == "get_pending_changes") {
            result["content"] = {{
                {"type", "text"},
                {"text", m_vivid.getPendingChanges().dump(2)}
            }};
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

            result["content"] = {{
                {"type", "text"},
                {"text", liveParams.dump(2)}
            }};
        }
        else if (name == "clear_pending_changes") {
            m_vivid.commitChanges();
            result["content"] = {{
                {"type", "text"},
                {"text", "Pending changes cleared."}
            }};
        }
        else if (name == "discard_pending_changes") {
            m_vivid.discardChanges();
            result["content"] = {{
                {"type", "text"},
                {"text", "Pending changes discarded. Parameters reverted to original values."}
            }};
        }
        else if (name == "get_runtime_status") {
            json status;
            status["connected"] = m_vivid.isConnected();
            status["compileStatus"] = m_vivid.getCompileStatus();
            status["operators"] = m_vivid.getOperators();
            status["pendingChanges"] = m_vivid.getPendingChanges()["hasChanges"];

            result["content"] = {{
                {"type", "text"},
                {"text", status.dump(2)}
            }};
        }
        else if (name == "list_operators") {
            auto& registry = OperatorRegistry::instance();
            const auto& ops = registry.operators();

            json opList = json::object();
            for (const auto& op : ops) {
                if (opList.find(op.category) == opList.end()) {
                    opList[op.category] = json::array();
                }

                json opInfo;
                opInfo["name"] = op.name;
                opInfo["description"] = op.description;
                opInfo["requiresInput"] = op.requiresInput;
                opInfo["outputType"] = outputKindName(op.outputKind);
                if (!op.addon.empty()) {
                    opInfo["addon"] = op.addon;
                }

                // Get parameters
                if (op.factory) {
                    try {
                        auto tempOp = op.factory();
                        auto params = tempOp->params();
                        opInfo["params"] = json::array();
                        for (const auto& p : params) {
                            opInfo["params"].push_back({
                                {"name", p.name},
                                {"min", p.minVal},
                                {"max", p.maxVal},
                                {"default", p.defaultVal[0]}
                            });
                        }
                    } catch (...) {}
                }

                opList[op.category].push_back(opInfo);
            }

            result["content"] = {{
                {"type", "text"},
                {"text", opList.dump(2)}
            }};
        }
        else if (name == "get_operator") {
            std::string opName = args.value("name", "");
            auto& registry = OperatorRegistry::instance();
            const auto* meta = registry.find(opName);

            if (!meta) {
                result["isError"] = true;
                result["content"] = {{
                    {"type", "text"},
                    {"text", "Operator '" + opName + "' not found."}
                }};
            } else {
                json opInfo;
                opInfo["name"] = meta->name;
                opInfo["category"] = meta->category;
                opInfo["description"] = meta->description;
                opInfo["requiresInput"] = meta->requiresInput;
                opInfo["outputType"] = outputKindName(meta->outputKind);
                if (!meta->addon.empty()) {
                    opInfo["addon"] = meta->addon;
                }

                // Get parameters
                if (meta->factory) {
                    try {
                        auto tempOp = meta->factory();
                        auto params = tempOp->params();
                        opInfo["params"] = json::array();
                        for (const auto& p : params) {
                            opInfo["params"].push_back({
                                {"name", p.name},
                                {"min", p.minVal},
                                {"max", p.maxVal},
                                {"default", p.defaultVal[0]}
                            });
                        }
                    } catch (...) {}
                }

                // Usage example
                opInfo["usage"] = "auto& op = chain.add<" + meta->name + ">(\"name\");";
                if (meta->requiresInput) {
                    opInfo["usage"] = opInfo["usage"].get<std::string>() + "\nop.input(&other);";
                }

                // Add enriched metadata from registry
                if (!meta->limitations.empty()) {
                    opInfo["limitations"] = meta->limitations;
                }
                if (!meta->related.empty()) {
                    opInfo["related"] = meta->related;
                }
                if (!meta->examples.empty()) {
                    opInfo["examples"] = meta->examples;
                }

                result["content"] = {{
                    {"type", "text"},
                    {"text", opInfo.dump(2)}
                }};
            }
        }
        else if (name == "search_docs") {
            std::string query = args.value("query", "");
            result["content"] = {{
                {"type", "text"},
                {"text", searchDocs(query)}
            }};
        }
        else if (name == "create_project") {
            std::string projectName = args.value("name", "");
            std::string parentPath = args.value("path", ".");
            std::string templateName = args.value("template", "blank");
            bool force = args.value("force", false);

            if (projectName.empty()) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Project name is required"}}};
                return result;
            }

            // Check if project directory already exists
            fs::path fullPath = fs::path(parentPath) / projectName;
            if (fs::exists(fullPath)) {
                if (force) {
                    // Remove existing directory
                    std::error_code ec;
                    fs::remove_all(fullPath, ec);
                    if (ec) {
                        result["isError"] = true;
                        result["content"] = {{{"type", "text"}, {"text", "Failed to remove existing directory: " + ec.message()}}};
                        return result;
                    }
                } else {
                    // Return helpful error with suggestions
                    json error;
                    error["error"] = "Directory already exists";
                    error["path"] = fs::absolute(fullPath).string();
                    error["suggestions"] = json::array({
                        "Use a different project name",
                        "Set force=true to replace the existing directory (WARNING: deletes all contents)",
                        "Manually delete the directory first"
                    });
                    result["isError"] = true;
                    result["content"] = {{{"type", "text"}, {"text", error.dump(2)}}};
                    return result;
                }
            }

            std::vector<std::string> cmdArgs = {
                getVividExecutable(), "new", projectName,
                "-y",  // Skip prompts
                "-t", templateName
            };

            // Add addons if specified
            if (args.contains("addons") && args["addons"].is_array()) {
                std::string addonList;
                for (const auto& addon : args["addons"]) {
                    if (!addonList.empty()) addonList += ",";
                    addonList += addon.get<std::string>();
                }
                if (!addonList.empty()) {
                    cmdArgs.push_back("-a");
                    cmdArgs.push_back(addonList);
                }
            }

            // Change to parent directory for the command
            std::string origDir = fs::current_path().string();
            try {
                fs::current_path(parentPath);
            } catch (...) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Invalid path: " + parentPath}}};
                return result;
            }

            auto cmdResult = runCommand(cmdArgs);
            fs::current_path(origDir);  // Restore directory

            if (cmdResult.exitCode == 0) {
                fs::path projectPath = fs::path(parentPath) / projectName;
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
        else if (name == "run_project") {
            std::string projectPath = args.value("path", "");

            if (projectPath.empty()) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Project path is required"}}};
                return result;
            }

            if (!fs::exists(projectPath)) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Project path does not exist: " + projectPath}}};
                return result;
            }

#ifdef _WIN32
            // Stop any existing instance first
            if (s_runningProcess != nullptr) {
                TerminateProcess(s_runningProcess, 0);
                CloseHandle(s_runningProcess);
                s_runningProcess = nullptr;
                s_runningPid = 0;
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }

            // Start process on Windows
            std::string exe = getVividExecutable();
            std::string cmdLine = "\"" + exe + "\" \"" + projectPath + "\"";

            STARTUPINFOA si = {};
            si.cb = sizeof(si);
            PROCESS_INFORMATION pi = {};

            if (CreateProcessA(nullptr, const_cast<char*>(cmdLine.c_str()),
                              nullptr, nullptr, FALSE,
                              CREATE_NEW_CONSOLE, nullptr, nullptr, &si, &pi)) {
                s_runningProcess = pi.hProcess;
                s_runningPid = pi.dwProcessId;
                CloseHandle(pi.hThread);

                std::this_thread::sleep_for(std::chrono::seconds(2));
                m_vivid.disconnect();
                bool connected = m_vivid.connect();

                json response;
                response["success"] = true;
                response["pid"] = static_cast<int>(s_runningPid);
                response["connected"] = connected;
                response["port"] = 9876;
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
            } else {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Failed to start project"}}};
            }
#else
            // Stop any existing instance first
            if (s_runningPid > 0) {
                kill(s_runningPid, SIGTERM);
                s_runningPid = 0;
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }

            // Fork and run vivid in background
            pid_t pid = fork();
            if (pid == 0) {
                // Child process
                std::string exe = getVividExecutable();
                execlp(exe.c_str(), exe.c_str(), projectPath.c_str(), nullptr);
                _exit(1);  // Only reached if exec fails
            } else if (pid > 0) {
                s_runningPid = pid;
                // Wait a moment for it to start
                std::this_thread::sleep_for(std::chrono::seconds(2));

                // Try to connect to verify it started
                m_vivid.disconnect();
                bool connected = m_vivid.connect();

                json response;
                response["success"] = true;
                response["pid"] = pid;
                response["connected"] = connected;
                response["port"] = 9876;
                result["content"] = {{{"type", "text"}, {"text", response.dump(2)}}};
            } else {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Failed to start project"}}};
            }
#endif
        }
        else if (name == "stop_project") {
#ifdef _WIN32
            if (s_runningProcess != nullptr) {
                TerminateProcess(s_runningProcess, 0);
                CloseHandle(s_runningProcess);
                s_runningProcess = nullptr;
                s_runningPid = 0;
                m_vivid.disconnect();
                result["content"] = {{{"type", "text"}, {"text", "Project stopped"}}};
            } else {
                result["content"] = {{{"type", "text"}, {"text", "No project is running"}}};
            }
#else
            if (s_runningPid > 0) {
                kill(s_runningPid, SIGTERM);
                int status;
                waitpid(s_runningPid, &status, WNOHANG);
                s_runningPid = 0;
                m_vivid.disconnect();
                result["content"] = {{{"type", "text"}, {"text", "Project stopped"}}};
            } else {
                result["content"] = {{{"type", "text"}, {"text", "No project is running"}}};
            }
#endif
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

            json response;
            response["valid"] = (cmdResult.exitCode == 0);
            if (cmdResult.exitCode != 0) {
                // Extract compile errors from output
                response["errors"] = cmdResult.output;
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
        else if (name == "list_addons") {
            std::vector<std::string> cmdArgs = {
                getVividExecutable(), "addons", "list"
            };

            auto cmdResult = runCommand(cmdArgs);
            result["content"] = {{{"type", "text"}, {"text", cmdResult.output}}};
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

    json handleResourcesList() {
        json resources = json::array();

        resources.push_back({
            {"uri", "vivid://docs/reference"},
            {"name", "Vivid Operator Reference"},
            {"description", "Core API reference for Vivid operators"},
            {"mimeType", "text/markdown"}
        });

        resources.push_back({
            {"uri", "vivid://docs/recipes"},
            {"name", "Vivid Recipes"},
            {"description", "Complete chain.cpp examples and patterns"},
            {"mimeType", "text/markdown"}
        });

        return {{"resources", resources}};
    }

    json handleResourcesRead(const json& params) {
        std::string uri = params.value("uri", "");
        json result;

        if (uri == "vivid://docs/reference") {
            result["contents"] = {{
                {"uri", uri},
                {"mimeType", "text/markdown"},
                {"text", loadDocsFile("LLM-REFERENCE.md")}
            }};
        } else if (uri == "vivid://docs/recipes") {
            result["contents"] = {{
                {"uri", uri},
                {"mimeType", "text/markdown"},
                {"text", loadDocsFile("RECIPES.md")}
            }};
        } else {
            result["contents"] = json::array();
        }

        return result;
    }

    std::string loadDocsFile(const std::string& filename) {
        // Search multiple locations for documentation files
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

    std::string searchDocs(const std::string& query) {
        // Simple search: load docs and find matching sections
        std::string refContent = loadDocsFile("LLM-REFERENCE.md");
        std::string recipesContent = loadDocsFile("RECIPES.md");

        std::string results;
        std::string queryLower = query;
        std::transform(queryLower.begin(), queryLower.end(), queryLower.begin(), ::tolower);

        // Search in reference
        if (!refContent.empty() && refContent.find("not found") == std::string::npos) {
            std::string refLower = refContent;
            std::transform(refLower.begin(), refLower.end(), refLower.begin(), ::tolower);
            if (refLower.find(queryLower) != std::string::npos) {
                results += "# From LLM-REFERENCE.md:\n\n";
                // Extract relevant section (simplified - just return first 2000 chars for now)
                results += refContent.substr(0, (std::min)(refContent.size(), size_t(2000)));
                results += "\n\n";
            }
        }

        // Search in recipes
        if (!recipesContent.empty() && recipesContent.find("not found") == std::string::npos) {
            std::string recipesLower = recipesContent;
            std::transform(recipesLower.begin(), recipesLower.end(), recipesLower.begin(), ::tolower);
            if (recipesLower.find(queryLower) != std::string::npos) {
                results += "# From RECIPES.md:\n\n";
                results += recipesContent.substr(0, (std::min)(recipesContent.size(), size_t(2000)));
            }
        }

        if (results.empty()) {
            results = "No documentation found matching '" + query + "'";
        }

        return results;
    }

    VividConnection m_vivid;
};

int runServer() {
    McpServer server;
    return server.run();
}

} // namespace vivid::mcp
