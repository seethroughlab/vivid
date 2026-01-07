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
#include <set>
#include <csignal>

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

// Helper to get discovered modules (relative to executable)
// Works for both dev builds (build/bin/vivid → build/modules/)
// and installed binaries (~/.vivid/bin/vivid → ~/.vivid/modules/)
std::vector<std::pair<std::string, fs::path>> getDiscoveredModules() {
    std::vector<std::pair<std::string, fs::path>> modules;

    // Get executable directory
    fs::path exeDir = fs::path(getVividExecutable()).parent_path();

    // Modules are at ../modules relative to bin/
    fs::path modulesDir = exeDir.parent_path() / "modules";

    if (fs::exists(modulesDir) && fs::is_directory(modulesDir)) {
        for (const auto& entry : fs::directory_iterator(modulesDir)) {
            if (entry.is_directory()) {
                // Check for module.json OR examples/ to identify as module
                fs::path moduleJson = entry.path() / "module.json";
                fs::path examplesDir = entry.path() / "examples";
                if (fs::exists(moduleJson) || fs::exists(examplesDir)) {
                    modules.push_back({entry.path().filename().string(), entry.path()});
                }
            }
        }
    }

    return modules;
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
        // Returns minimal info: name, category, module. For details, read the header files.
        tools.push_back({
            {"name", "list_operators"},
            {"description", "List all Vivid operators with name, category, and source module. For API details, read the operator's header file (grep for 'class OperatorName' in include/)."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", json::object()}
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
                    {"modules", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Modules to include (use list_modules to see available)"}}},
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

        // list_modules - List available modules
        tools.push_back({
            {"name", "list_modules"},
            {"description", "List installed Vivid modules."},
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
            json opList = registry.toJsonGrouped();

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
                if (!meta->module.empty()) {
                    opInfo["module"] = meta->module;
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
                if (!meta->api.empty()) {
                    opInfo["api"] = meta->api;
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
        else if (name == "search_operators") {
            std::string query = args.value("query", "");
            if (query.empty()) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Query is required"}}};
                return result;
            }

            // Convert query to lowercase for case-insensitive matching
            std::string queryLower = query;
            std::transform(queryLower.begin(), queryLower.end(), queryLower.begin(), ::tolower);

            // Split query into words
            std::vector<std::string> queryWords;
            std::istringstream iss(queryLower);
            std::string word;
            while (iss >> word) {
                if (word.length() >= 2) {
                    queryWords.push_back(word);
                }
            }

            auto& registry = OperatorRegistry::instance();
            json matches = json::array();

            // Helper to check if text contains any query word
            auto containsQuery = [&](const std::string& text) {
                std::string textLower = text;
                std::transform(textLower.begin(), textLower.end(), textLower.begin(), ::tolower);
                for (const auto& qword : queryWords) {
                    if (textLower.find(qword) != std::string::npos) {
                        return true;
                    }
                }
                return false;
            };

            for (const auto& meta : registry.operators()) {
                bool match = containsQuery(meta.name) ||
                             containsQuery(meta.description) ||
                             containsQuery(meta.category);

                // Check related operators
                if (!match) {
                    for (const auto& rel : meta.related) {
                        if (containsQuery(rel)) {
                            match = true;
                            break;
                        }
                    }
                }

                if (match) {
                    matches.push_back({
                        {"name", meta.name},
                        {"category", meta.category},
                        {"description", meta.description}
                    });
                }
            }

            if (matches.empty()) {
                result["content"] = {{
                    {"type", "text"},
                    {"text", "No operators found matching '" + query + "'"}
                }};
            } else {
                result["content"] = {{
                    {"type", "text"},
                    {"text", matches.dump(2)}
                }};
            }
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
        else if (name == "list_modules") {
            std::vector<std::string> cmdArgs = {
                getVividExecutable(), "modules", "list"
            };

            auto cmdResult = runCommand(cmdArgs);
            result["content"] = {{{"type", "text"}, {"text", cmdResult.output}}};
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
        else if (name == "get_example") {
            std::string examplePath = args.value("path", "");
            if (examplePath.empty()) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "Example path is required"}}};
                return result;
            }

            std::string doc = loadExampleDoc(examplePath);
            if (doc.empty()) {
                result["isError"] = true;
                result["content"] = {{{"type", "text"}, {"text", "No CLAUDE.md found for example: " + examplePath}}};
            } else {
                result["content"] = {{{"type", "text"}, {"text", doc}}};
            }
        }
        else if (name == "list_examples") {
            // List examples from known locations
            json examples = json::object();

            // Helper to scan a directory for examples
            auto scanExamples = [this](const fs::path& baseDir, const std::string& category) {
                json categoryExamples = json::array();
                if (fs::exists(baseDir) && fs::is_directory(baseDir)) {
                    for (const auto& entry : fs::directory_iterator(baseDir)) {
                        if (entry.is_directory()) {
                            fs::path chainFile = entry.path() / "chain.cpp";
                            if (fs::exists(chainFile)) {
                                json example;
                                example["name"] = entry.path().filename().string();
                                example["path"] = entry.path().string();

                                // Try to get description from CLAUDE.md
                                fs::path claudeFile = entry.path() / "CLAUDE.md";
                                if (fs::exists(claudeFile)) {
                                    std::ifstream file(claudeFile);
                                    if (file) {
                                        std::string line;
                                        // Read first non-empty, non-header line as description
                                        while (std::getline(file, line)) {
                                            if (!line.empty() && line[0] != '#' && line[0] != '-') {
                                                example["description"] = line;
                                                break;
                                            }
                                        }
                                    }
                                    example["hasDoc"] = true;
                                } else {
                                    example["hasDoc"] = false;
                                }

                                categoryExamples.push_back(example);
                            }
                        }
                    }
                }
                return categoryExamples;
            };

            // Get root relative to executable (works for dev builds and installed binaries)
            fs::path exeDir = fs::path(getVividExecutable()).parent_path();
            fs::path rootDir = exeDir.parent_path();

            // Core examples (in src/vivid-core/examples/ for dev, or examples/ for installed)
            fs::path coreExamplesDir = rootDir / "src" / "vivid-core" / "examples";
            if (!fs::exists(coreExamplesDir)) {
                coreExamplesDir = rootDir / "examples";  // Installed layout
            }

            if (fs::exists(coreExamplesDir)) {
                auto coreExamples = scanExamples(coreExamplesDir / "2d-effects", "2D Effects");
                if (!coreExamples.empty()) examples["Core - 2D Effects"] = coreExamples;

                coreExamples = scanExamples(coreExamplesDir / "utility", "Utility");
                if (!coreExamples.empty()) examples["Core - Utility"] = coreExamples;
            }

            // Module examples - discover dynamically
            for (const auto& [moduleName, modulePath] : getDiscoveredModules()) {
                auto moduleExamples = scanExamples(modulePath / "examples", moduleName);
                if (!moduleExamples.empty()) {
                    examples[std::string("Module - ") + moduleName] = moduleExamples;
                }
            }

            // Getting started examples
            fs::path projectsDir = rootDir / "projects";
            if (fs::exists(projectsDir)) {
                auto gettingStarted = scanExamples(projectsDir / "getting-started", "Getting Started");
                if (!gettingStarted.empty()) examples["Getting Started"] = gettingStarted;

                // Showcase examples
                auto showcase = scanExamples(projectsDir / "showcase", "Showcase");
                if (!showcase.empty()) examples["Showcase"] = showcase;
            }

            result["content"] = {{{"type", "text"}, {"text", examples.dump(2)}}};
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

        if (uri == "vivid://docs/recipes") {
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

    std::string loadModuleReadme(const std::string& moduleName) {
        // Search for module README.md in various locations
        std::vector<fs::path> searchPaths;

        // 1. Current working directory (common for dev builds)
        searchPaths.push_back(fs::current_path() / "modules" / moduleName / "README.md");

        // 2. Paths relative to executable
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
        char pathBuf[4096];
        ssize_t len = readlink("/proc/self/exe", pathBuf, sizeof(pathBuf) - 1);
        if (len != -1) {
            pathBuf[len] = '\0';
            exeDir = fs::path(pathBuf).parent_path();
        }
#endif

        if (!exeDir.empty()) {
            // build/bin/vivid -> modules (go up 2 levels)
            searchPaths.push_back(exeDir.parent_path().parent_path() / "modules" / moduleName / "README.md");
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

        return "";  // Not found - don't add error message to search
    }

    std::string loadExampleDoc(const std::string& examplePath) {
        // Load CLAUDE.md from an example directory
        std::vector<fs::path> searchPaths;

        // 1. Current working directory
        searchPaths.push_back(fs::current_path() / examplePath / "CLAUDE.md");

        // 2. Paths relative to executable
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
        char pathBuf[4096];
        ssize_t len = readlink("/proc/self/exe", pathBuf, sizeof(pathBuf) - 1);
        if (len != -1) {
            pathBuf[len] = '\0';
            exeDir = fs::path(pathBuf).parent_path();
        }
#endif

        if (!exeDir.empty()) {
            // build/bin/vivid -> project root (go up 2 levels)
            searchPaths.push_back(exeDir.parent_path().parent_path() / examplePath / "CLAUDE.md");
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

        return "";
    }

    // Split a string into words, filtering out common stop words
    std::vector<std::string> splitIntoWords(const std::string& text) {
        std::vector<std::string> words;
        std::istringstream iss(text);
        std::string word;
        // Common stop words to ignore
        static const std::set<std::string> stopWords = {
            "a", "an", "the", "is", "are", "was", "were", "be", "been",
            "to", "of", "in", "for", "on", "with", "at", "by", "from",
            "or", "and", "it", "as", "do", "how", "what", "which"
        };
        while (iss >> word) {
            // Remove punctuation and convert to lowercase
            word.erase(std::remove_if(word.begin(), word.end(), ::ispunct), word.end());
            std::transform(word.begin(), word.end(), word.begin(), ::tolower);
            if (word.length() >= 2 && stopWords.find(word) == stopWords.end()) {
                words.push_back(word);
            }
        }
        return words;
    }

    // Parse markdown into sections (## headers and their content)
    struct DocSection {
        std::string header;
        std::string content;
        std::string contentLower;
    };

    std::vector<DocSection> parseMarkdownSections(const std::string& content) {
        std::vector<DocSection> sections;
        std::istringstream stream(content);
        std::string line;
        DocSection current;

        while (std::getline(stream, line)) {
            // Check for headers (##, ###, etc.) - not just top-level #
            if (line.length() >= 2 && line[0] == '#' && line[1] == '#') {
                // Save previous section if it has content
                if (!current.header.empty() || !current.content.empty()) {
                    current.contentLower = current.content;
                    std::transform(current.contentLower.begin(), current.contentLower.end(),
                                   current.contentLower.begin(), ::tolower);
                    sections.push_back(current);
                }
                // Start new section
                current.header = line;
                current.content.clear();
            } else {
                current.content += line + "\n";
            }
        }
        // Don't forget the last section
        if (!current.header.empty() || !current.content.empty()) {
            current.contentLower = current.content;
            std::transform(current.contentLower.begin(), current.contentLower.end(),
                           current.contentLower.begin(), ::tolower);
            sections.push_back(current);
        }
        return sections;
    }

    // Count how many query words match in a section
    int countMatches(const DocSection& section, const std::vector<std::string>& queryWords) {
        int count = 0;
        std::string headerLower = section.header;
        std::transform(headerLower.begin(), headerLower.end(), headerLower.begin(), ::tolower);

        for (const auto& word : queryWords) {
            // Check both header and content, header matches count double
            if (headerLower.find(word) != std::string::npos) {
                count += 2;
            }
            if (section.contentLower.find(word) != std::string::npos) {
                count += 1;
            }
        }
        return count;
    }

    std::string searchDocs(const std::string& query) {
        // Search examples and conceptual docs (not operator reference - use get_operator for that)
        std::vector<std::pair<std::string, std::string>> docs = {
            {"RECIPES.md", loadDocsFile("RECIPES.md")},                     // Full chain examples
            {"CHAIN-API.md", loadDocsFile("CHAIN-API.md")},                 // Chain concepts and API
            {"CANVAS-API.md", loadDocsFile("CANVAS-API.md")},               // 2D canvas drawing API
            {"ERROR-REFERENCE.md", loadDocsFile("ERROR-REFERENCE.md")},     // Debugging help
            {"CREATING-OPERATORS.md", loadDocsFile("CREATING-OPERATORS.md")}, // Custom operators
        };

        // Dynamically discover module READMEs
        // Check both build/modules/ and source modules/ directories
        for (const auto& [moduleName, modulePath] : getDiscoveredModules()) {
            fs::path readme = modulePath / "README.md";

            // If not in build dir, try source dir
            if (!fs::exists(readme)) {
                fs::path exeDir = fs::path(getVividExecutable()).parent_path();
                fs::path sourceModules = exeDir.parent_path().parent_path() / "modules" / moduleName / "README.md";
                if (fs::exists(sourceModules)) {
                    readme = sourceModules;
                }
            }

            if (fs::exists(readme)) {
                std::ifstream file(readme);
                if (file) {
                    std::stringstream buffer;
                    buffer << file.rdbuf();
                    docs.push_back({moduleName + "/README.md", buffer.str()});
                }
            }
        }

        // Split query into searchable words
        std::vector<std::string> queryWords = splitIntoWords(query);
        if (queryWords.empty()) {
            return "No valid search terms in query: '" + query + "'";
        }

        // Collect matching sections with scores
        struct ScoredSection {
            std::string source;
            std::string header;
            std::string content;
            int score;
        };
        std::vector<ScoredSection> matches;

        // Search each document
        for (const auto& [docName, docContent] : docs) {
            if (!docContent.empty() && docContent.rfind("Documentation file not found:", 0) != 0) {
                auto sections = parseMarkdownSections(docContent);
                for (const auto& section : sections) {
                    int score = countMatches(section, queryWords);
                    if (score > 0) {
                        matches.push_back({docName, section.header, section.content, score});
                    }
                }
            }
        }

        if (matches.empty()) {
            return "No documentation found matching '" + query + "'";
        }

        // Sort by score descending
        std::sort(matches.begin(), matches.end(),
                  [](const ScoredSection& a, const ScoredSection& b) { return a.score > b.score; });

        // Build result - return top 5 matches, max ~4000 chars total
        std::string results;
        size_t totalChars = 0;
        const size_t maxChars = 4000;
        int count = 0;

        for (const auto& match : matches) {
            if (count >= 5 || totalChars >= maxChars) break;

            std::string sectionText = match.header + "\n" + match.content;

            // Truncate very long sections
            if (sectionText.size() > 1500) {
                sectionText = sectionText.substr(0, 1500) + "\n...(truncated)\n";
            }

            if (count == 0 || matches[count - 1].source != match.source) {
                results += "# From " + match.source + ":\n\n";
            }
            results += sectionText + "\n";
            totalChars += sectionText.size();
            count++;
        }

        return results;
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
