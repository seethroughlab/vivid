// Windows: prevent min/max macros from conflicting with std::min/max
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <vivid/editor_bridge.h>
#include <vivid/operator.h>
#include <ixwebsocket/IXWebSocketServer.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <mutex>
#include <chrono>

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
                    else if (type == "window_control") {
                        std::string setting = j.value("setting", "");
                        int value = j.value("value", 0);
                        std::cout << "[EditorBridge] Window control: " << setting << " = " << value << "\n";
                        if (m_windowControlCallback) {
                            m_windowControlCallback(setting, value);
                        }
                    }
                    else if (type == "request_window_state") {
                        std::cout << "[EditorBridge] Window state requested\n";
                        // The main loop will handle this by calling sendWindowState
                        if (m_requestOperatorsCallback) {
                            // Piggyback on operator request to trigger window state send
                            // (main.cpp handles both in the same callback area)
                            m_requestOperatorsCallback();
                        }
                    }
                    else if (type == "commit_changes") {
                        std::cout << "[EditorBridge] Commit pending changes\n";
                        commitPendingChanges();
                    }
                    else if (type == "discard_changes") {
                        std::cout << "[EditorBridge] Discard pending changes\n";
                        auto discarded = discardPendingChanges();
                        if (m_discardChangesCallback && !discarded.empty()) {
                            m_discardChangesCallback(discarded);
                        }
                    }
                    else if (type == "request_pending_changes") {
                        std::cout << "[EditorBridge] Pending changes requested\n";
                        sendPendingChanges();
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

void EditorBridge::sendWindowState(const EditorWindowState& state) {
    if (!m_running || !m_impl) return;

    json j;
    j["type"] = "window_state";
    j["fullscreen"] = state.fullscreen;
    j["borderless"] = state.borderless;
    j["alwaysOnTop"] = state.alwaysOnTop;
    j["cursorVisible"] = state.cursorVisible;
    j["currentMonitor"] = state.currentMonitor;

    j["monitors"] = json::array();
    for (const auto& m : state.monitors) {
        json mJson;
        mJson["index"] = m.index;
        mJson["name"] = m.name;
        mJson["width"] = m.width;
        mJson["height"] = m.height;
        j["monitors"].push_back(mJson);
    }

    std::string msg = j.dump();

    // Broadcast to all clients
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto& client : m_impl->server.getClients()) {
        client->send(msg);
    }
}

void EditorBridge::addPendingChange(const PendingChange& change) {
    // Check if we already have a pending change for this operator.param
    for (auto& existing : m_pendingChanges) {
        if (existing.operatorName == change.operatorName &&
            existing.paramName == change.paramName) {
            // Update the new value, keep the original old value
            for (int i = 0; i < 4; ++i) {
                existing.newValue[i] = change.newValue[i];
            }
            existing.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            std::cout << "[EditorBridge] Updated pending change: " << change.operatorName
                      << "." << change.paramName << "\n";
            sendPendingChanges();
            return;
        }
    }

    // New pending change
    PendingChange newChange = change;
    newChange.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    m_pendingChanges.push_back(newChange);
    std::cout << "[EditorBridge] Added pending change: " << change.operatorName
              << "." << change.paramName << " (total: " << m_pendingChanges.size() << ")\n";
    sendPendingChanges();
}

void EditorBridge::sendPendingChanges() {
    if (!m_running || !m_impl) return;

    json j;
    j["type"] = "pending_changes";
    j["hasChanges"] = !m_pendingChanges.empty();
    j["changes"] = json::array();

    for (const auto& change : m_pendingChanges) {
        json cJson;
        cJson["operator"] = change.operatorName;
        cJson["param"] = change.paramName;
        cJson["paramType"] = change.paramType;
        cJson["oldValue"] = {change.oldValue[0], change.oldValue[1], change.oldValue[2], change.oldValue[3]};
        cJson["newValue"] = {change.newValue[0], change.newValue[1], change.newValue[2], change.newValue[3]};
        cJson["sourceLine"] = change.sourceLine;
        cJson["timestamp"] = change.timestamp;
        j["changes"].push_back(cJson);
    }

    std::string msg = j.dump();

    // Broadcast to all clients
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto& client : m_impl->server.getClients()) {
        client->send(msg);
    }
}

void EditorBridge::commitPendingChanges() {
    std::cout << "[EditorBridge] Committing " << m_pendingChanges.size() << " pending changes\n";
    m_pendingChanges.clear();
    sendPendingChanges();
}

std::vector<PendingChange> EditorBridge::discardPendingChanges() {
    std::cout << "[EditorBridge] Discarding " << m_pendingChanges.size() << " pending changes\n";
    std::vector<PendingChange> discarded = std::move(m_pendingChanges);
    m_pendingChanges.clear();
    sendPendingChanges();
    return discarded;
}

} // namespace vivid
