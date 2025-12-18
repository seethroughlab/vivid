#include <vivid/editor_bridge.h>
#include <vivid/operator.h>
#include <ixwebsocket/IXWebSocketServer.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <mutex>

using json = nlohmann::json;

namespace vivid {

class EditorBridge::Impl {
public:
    ix::WebSocketServer server;
    std::mutex mutex;
    int port = 9876;

    Impl(int p) : server(p, "0.0.0.0"), port(p) {}
};

EditorBridge::EditorBridge() : m_impl(std::make_unique<Impl>(9876)) {}

EditorBridge::~EditorBridge() {
    stop();
}

void EditorBridge::start(int port) {
    if (m_running) return;

    m_port = port;
    m_impl = std::make_unique<Impl>(port);

    m_impl->server.setOnClientMessageCallback(
        [this](std::shared_ptr<ix::ConnectionState> state,
               ix::WebSocket& ws,
               const ix::WebSocketMessagePtr& msg) {

            if (msg->type == ix::WebSocketMessageType::Open) {
                std::cout << "[EditorBridge] Client connected from " << state->getRemoteIp() << "\n";
            }
            else if (msg->type == ix::WebSocketMessageType::Close) {
                std::cout << "[EditorBridge] Client disconnected\n";
            }
            else if (msg->type == ix::WebSocketMessageType::Message) {
                // Parse incoming command
                try {
                    json j = json::parse(msg->str);
                    std::string type = j.value("type", "");

                    if (type == "reload") {
                        std::cout << "[EditorBridge] Reload command received\n";
                        if (m_reloadCallback) {
                            m_reloadCallback("reload");
                        }
                    }
                    else if (type == "param_change") {
                        std::string opName = j.value("operator", "");
                        std::string paramName = j.value("param", "");
                        float value[4] = {0};
                        if (j.contains("value") && j["value"].is_array()) {
                            const auto& arr = j["value"];
                            for (size_t i = 0; i < std::min(arr.size(), size_t(4)); ++i) {
                                value[i] = arr[i].get<float>();
                            }
                            std::cout << "[EditorBridge] Param change: " << opName << "." << paramName << "\n";
                            if (m_paramChangeCallback) {
                                m_paramChangeCallback(opName, paramName, value);
                            }
                        }
                    }
                    else if (type == "solo_node") {
                        std::string opName = j.value("operator", "");
                        std::cout << "[EditorBridge] Solo node: " << opName << "\n";
                        if (m_soloNodeCallback) {
                            m_soloNodeCallback(opName);
                        }
                    }
                    else if (type == "solo_exit") {
                        std::cout << "[EditorBridge] Solo exit\n";
                        if (m_soloExitCallback) {
                            m_soloExitCallback();
                        }
                    }
                    else if (type == "select_node") {
                        std::string opName = j.value("operator", "");
                        std::cout << "[EditorBridge] Select node: " << opName << "\n";
                        if (m_selectNodeCallback) {
                            m_selectNodeCallback(opName);
                        }
                    }
                    else if (type == "focused_node") {
                        std::string opName = j.value("operator", "");
                        // Empty operator name means clear focus
                        if (opName.empty()) {
                            std::cout << "[EditorBridge] Clear focused node\n";
                        } else {
                            std::cout << "[EditorBridge] Focused node: " << opName << "\n";
                        }
                        if (m_focusedNodeCallback) {
                            m_focusedNodeCallback(opName);
                        }
                    }
                    else if (type == "request_operators") {
                        std::cout << "[EditorBridge] Operators requested\n";
                        if (m_requestOperatorsCallback) {
                            m_requestOperatorsCallback();
                        }
                    }
                } catch (const json::exception& e) {
                    std::cerr << "[EditorBridge] JSON parse error: " << e.what() << "\n";
                }
            }
            else if (msg->type == ix::WebSocketMessageType::Error) {
                std::cerr << "[EditorBridge] Error: " << msg->errorInfo.reason << "\n";
            }
        }
    );

    auto res = m_impl->server.listen();
    if (!res.first) {
        std::cerr << "[EditorBridge] Failed to start on port " << port << ": " << res.second << "\n";
        return;
    }

    m_impl->server.start();
    m_running = true;
    std::cout << "[EditorBridge] Listening on port " << port << "\n";
}

void EditorBridge::stop() {
    if (!m_running) return;

    m_impl->server.stop();
    m_running = false;
    std::cout << "[EditorBridge] Stopped\n";
}

size_t EditorBridge::clientCount() const {
    if (!m_impl) return 0;
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->server.getClients().size();
}

void EditorBridge::sendCompileStatus(bool success, const std::string& message) {
    if (!m_running || !m_impl) return;

    json j;
    j["type"] = "compile_status";
    j["success"] = success;
    j["message"] = message;

    std::string msg = j.dump();

    // Broadcast to all clients
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto& client : m_impl->server.getClients()) {
        client->send(msg);
    }
}

void EditorBridge::sendOperatorList(const std::vector<EditorOperatorInfo>& operators) {
    std::cout << "[EditorBridge] sendOperatorList called with " << operators.size() << " operators\n";
    if (!m_running || !m_impl) return;

    json j;
    j["type"] = "operator_list";
    j["operators"] = json::array();

    for (const auto& op : operators) {
        json opJson;
        opJson["name"] = op.chainName;
        opJson["displayName"] = op.displayName;
        opJson["outputType"] = op.outputType;
        opJson["sourceLine"] = op.sourceLine;
        opJson["inputs"] = op.inputNames;
        j["operators"].push_back(opJson);
    }

    std::string msg = j.dump();

    // Broadcast to all clients
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto& client : m_impl->server.getClients()) {
        client->send(msg);
    }
}

void EditorBridge::sendParamValues(const std::vector<EditorParamInfo>& params) {
    if (!m_running || !m_impl) return;

    json j;
    j["type"] = "param_values";
    j["params"] = json::array();

    for (const auto& p : params) {
        json paramJson;
        paramJson["operator"] = p.operatorName;
        paramJson["name"] = p.paramName;
        paramJson["type"] = p.paramType;
        paramJson["value"] = {p.value[0], p.value[1], p.value[2], p.value[3]};
        paramJson["min"] = p.minVal;
        paramJson["max"] = p.maxVal;

        // Include string fields for String/FilePath types
        if (!p.stringValue.empty() || p.paramType == "FilePath" || p.paramType == "String") {
            paramJson["stringValue"] = p.stringValue;
        }
        if (!p.fileFilter.empty()) {
            paramJson["fileFilter"] = p.fileFilter;
        }
        if (!p.fileCategory.empty()) {
            paramJson["fileCategory"] = p.fileCategory;
        }

        j["params"].push_back(paramJson);
    }

    std::string msg = j.dump();

    // Broadcast to all clients
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto& client : m_impl->server.getClients()) {
        client->send(msg);
    }
}

void EditorBridge::sendPerformanceStats(const EditorPerformanceStats& stats) {
    if (!m_running || !m_impl) return;

    json j;
    j["type"] = "performance_stats";
    j["fps"] = stats.fps;
    j["frameTimeMs"] = stats.frameTimeMs;
    j["fpsHistory"] = stats.fpsHistory;
    j["frameTimeHistory"] = stats.frameTimeHistory;
    j["textureMemoryBytes"] = stats.textureMemoryBytes;
    j["operatorCount"] = stats.operatorCount;

    j["operatorTimings"] = json::array();
    for (const auto& t : stats.operatorTimings) {
        j["operatorTimings"].push_back({{"name", t.name}, {"timeMs", t.timeMs}});
    }

    std::string msg = j.dump();

    // Broadcast to all clients
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto& client : m_impl->server.getClients()) {
        client->send(msg);
    }
}

void EditorBridge::sendSoloState(bool active, const std::string& operatorName) {
    if (!m_running || !m_impl) return;

    json j;
    j["type"] = "solo_state";
    j["active"] = active;
    if (active && !operatorName.empty()) {
        j["operator"] = operatorName;
    }

    std::string msg = j.dump();

    // Broadcast to all clients
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto& client : m_impl->server.getClients()) {
        client->send(msg);
    }
}

} // namespace vivid
