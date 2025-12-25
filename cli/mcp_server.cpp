// Vivid MCP Server
// Implements Model Context Protocol over stdio for Claude Code integration
// Connects to running Vivid instance via WebSocket to provide live state access

#include <vivid/cli.h>
#include <vivid/operator_registry.h>
#include <nlohmann/json.hpp>
#include <ixwebsocket/IXWebSocket.h>
#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <algorithm>

#ifdef __APPLE__
#include <mach-o/dyld.h>
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
                std::cout << response.dump() << "\n" << std::flush;
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
        } else if (method == "initialized") {
            // Notification, no response needed
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
        // Try ~/.vivid/docs first, then build directory
        std::vector<fs::path> searchPaths = {
            fs::path(getenv("HOME") ? getenv("HOME") : ".") / ".vivid" / "docs" / filename,
        };

        // Add build directory paths
        char pathBuf[4096];
#ifdef __APPLE__
        uint32_t size = sizeof(pathBuf);
        if (_NSGetExecutablePath(pathBuf, &size) == 0) {
            fs::path exeDir = fs::path(pathBuf).parent_path();
            searchPaths.push_back(exeDir.parent_path().parent_path() / "docs" / filename);
        }
#endif

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
                results += refContent.substr(0, std::min(refContent.size(), size_t(2000)));
                results += "\n\n";
            }
        }

        // Search in recipes
        if (!recipesContent.empty() && recipesContent.find("not found") == std::string::npos) {
            std::string recipesLower = recipesContent;
            std::transform(recipesLower.begin(), recipesLower.end(), recipesLower.begin(), ::tolower);
            if (recipesLower.find(queryLower) != std::string::npos) {
                results += "# From RECIPES.md:\n\n";
                results += recipesContent.substr(0, std::min(recipesContent.size(), size_t(2000)));
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
