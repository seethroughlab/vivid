#include <vivid/editor_bridge.h>
#include <vivid/operator.h>
#include <ixwebsocket/IXWebSocketServer.h>
#include <iostream>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace vivid {

// JSON helpers (simple, no external dependency)
namespace {
    std::string jsonEscape(const std::string& s) {
        std::ostringstream ss;
        for (char c : s) {
            switch (c) {
                case '"':  ss << "\\\""; break;
                case '\\': ss << "\\\\"; break;
                case '\n': ss << "\\n"; break;
                case '\r': ss << "\\r"; break;
                case '\t': ss << "\\t"; break;
                default:   ss << c; break;
            }
        }
        return ss.str();
    }

    std::string parseJsonType(const std::string& json) {
        // Simple parser for {"type": "value"} - good enough for our protocol
        auto pos = json.find("\"type\"");
        if (pos == std::string::npos) return "";
        pos = json.find(':', pos);
        if (pos == std::string::npos) return "";
        pos = json.find('"', pos);
        if (pos == std::string::npos) return "";
        auto end = json.find('"', pos + 1);
        if (end == std::string::npos) return "";
        return json.substr(pos + 1, end - pos - 1);
    }

    std::string parseJsonString(const std::string& json, const std::string& key) {
        std::string searchKey = "\"" + key + "\"";
        auto pos = json.find(searchKey);
        if (pos == std::string::npos) return "";
        pos = json.find(':', pos);
        if (pos == std::string::npos) return "";
        pos = json.find('"', pos);
        if (pos == std::string::npos) return "";
        auto end = json.find('"', pos + 1);
        if (end == std::string::npos) return "";
        return json.substr(pos + 1, end - pos - 1);
    }

    bool parseJsonFloatArray(const std::string& json, const std::string& key, float out[4]) {
        std::string searchKey = "\"" + key + "\"";
        auto pos = json.find(searchKey);
        if (pos == std::string::npos) return false;
        pos = json.find('[', pos);
        if (pos == std::string::npos) return false;
        auto end = json.find(']', pos);
        if (end == std::string::npos) return false;

        std::string arr = json.substr(pos + 1, end - pos - 1);
        int idx = 0;
        size_t start = 0;
        while (idx < 4 && start < arr.size()) {
            size_t comma = arr.find(',', start);
            if (comma == std::string::npos) comma = arr.size();
            std::string numStr = arr.substr(start, comma - start);
            try {
                out[idx++] = std::stof(numStr);
            } catch (...) {
                break;
            }
            start = comma + 1;
        }
        return idx > 0;
    }

    const char* paramTypeName(ParamType type) {
        switch (type) {
            case ParamType::Float:  return "Float";
            case ParamType::Int:    return "Int";
            case ParamType::Bool:   return "Bool";
            case ParamType::Vec2:   return "Vec2";
            case ParamType::Vec3:   return "Vec3";
            case ParamType::Vec4:   return "Vec4";
            case ParamType::Color:  return "Color";
            case ParamType::String: return "String";
            default:                return "Unknown";
        }
    }
}

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
                std::string type = parseJsonType(msg->str);
                if (type == "reload") {
                    std::cout << "[EditorBridge] Reload command received\n";
                    if (m_reloadCallback) {
                        m_reloadCallback("reload");
                    }
                }
                else if (type == "param_change") {
                    std::string opName = parseJsonString(msg->str, "operator");
                    std::string paramName = parseJsonString(msg->str, "param");
                    float value[4] = {0};
                    if (parseJsonFloatArray(msg->str, "value", value)) {
                        std::cout << "[EditorBridge] Param change: " << opName << "." << paramName << "\n";
                        if (m_paramChangeCallback) {
                            m_paramChangeCallback(opName, paramName, value);
                        }
                    }
                }
                else if (type == "solo_node") {
                    std::string opName = parseJsonString(msg->str, "operator");
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
                    std::string opName = parseJsonString(msg->str, "operator");
                    std::cout << "[EditorBridge] Select node: " << opName << "\n";
                    if (m_selectNodeCallback) {
                        m_selectNodeCallback(opName);
                    }
                }
                else if (type == "focused_node") {
                    std::string opName = parseJsonString(msg->str, "operator");
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

    // Build JSON message
    std::ostringstream json;
    json << "{\"type\":\"compile_status\",\"success\":" << (success ? "true" : "false");
    json << ",\"message\":\"" << jsonEscape(message) << "\"}";

    std::string msg = json.str();

    // Broadcast to all clients
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto& client : m_impl->server.getClients()) {
        client->send(msg);
    }
}

void EditorBridge::sendOperatorList(const std::vector<EditorOperatorInfo>& operators) {
    std::cout << "[EditorBridge] sendOperatorList called with " << operators.size() << " operators\n";
    if (!m_running || !m_impl) return;

    // Build JSON message
    std::ostringstream json;
    json << "{\"type\":\"operator_list\",\"operators\":[";

    for (size_t i = 0; i < operators.size(); ++i) {
        const auto& op = operators[i];
        if (i > 0) json << ",";
        json << "{";
        json << "\"name\":\"" << jsonEscape(op.chainName) << "\",";
        json << "\"displayName\":\"" << jsonEscape(op.displayName) << "\",";
        json << "\"outputType\":\"" << jsonEscape(op.outputType) << "\",";
        json << "\"sourceLine\":" << op.sourceLine << ",";
        json << "\"inputs\":[";
        for (size_t j = 0; j < op.inputNames.size(); ++j) {
            if (j > 0) json << ",";
            json << "\"" << jsonEscape(op.inputNames[j]) << "\"";
        }
        json << "]}";
    }

    json << "]}";

    std::string msg = json.str();

    // Broadcast to all clients
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto& client : m_impl->server.getClients()) {
        client->send(msg);
    }
}

void EditorBridge::sendParamValues(const std::vector<EditorParamInfo>& params) {
    if (!m_running || !m_impl) return;

    // Build JSON message
    std::ostringstream json;
    json << std::fixed;  // Consistent float formatting
    json << "{\"type\":\"param_values\",\"params\":[";

    for (size_t i = 0; i < params.size(); ++i) {
        const auto& p = params[i];
        if (i > 0) json << ",";
        json << "{";
        json << "\"operator\":\"" << jsonEscape(p.operatorName) << "\",";
        json << "\"name\":\"" << jsonEscape(p.paramName) << "\",";
        json << "\"type\":\"" << jsonEscape(p.paramType) << "\",";
        json << "\"value\":[" << p.value[0] << "," << p.value[1] << "," << p.value[2] << "," << p.value[3] << "],";
        json << "\"min\":" << p.minVal << ",";
        json << "\"max\":" << p.maxVal;
        // Include string fields for String/FilePath types
        if (!p.stringValue.empty() || p.paramType == "FilePath" || p.paramType == "String") {
            json << ",\"stringValue\":\"" << jsonEscape(p.stringValue) << "\"";
        }
        if (!p.fileFilter.empty()) {
            json << ",\"fileFilter\":\"" << jsonEscape(p.fileFilter) << "\"";
        }
        if (!p.fileCategory.empty()) {
            json << ",\"fileCategory\":\"" << jsonEscape(p.fileCategory) << "\"";
        }
        json << "}";
    }

    json << "]}";

    std::string msg = json.str();

    // Broadcast to all clients
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto& client : m_impl->server.getClients()) {
        client->send(msg);
    }
}

void EditorBridge::sendPerformanceStats(const EditorPerformanceStats& stats) {
    if (!m_running || !m_impl) return;

    // Build JSON message
    std::ostringstream json;
    json << std::fixed;
    json << std::setprecision(2);
    json << "{\"type\":\"performance_stats\",";

    // Frame timing
    json << "\"fps\":" << stats.fps << ",";
    json << "\"frameTimeMs\":" << stats.frameTimeMs << ",";

    // FPS history
    json << "\"fpsHistory\":[";
    for (size_t i = 0; i < stats.fpsHistory.size(); ++i) {
        if (i > 0) json << ",";
        json << stats.fpsHistory[i];
    }
    json << "],";

    // Frame time history
    json << "\"frameTimeHistory\":[";
    for (size_t i = 0; i < stats.frameTimeHistory.size(); ++i) {
        if (i > 0) json << ",";
        json << stats.frameTimeHistory[i];
    }
    json << "],";

    // Memory and operator count
    json << "\"textureMemoryBytes\":" << stats.textureMemoryBytes << ",";
    json << "\"operatorCount\":" << stats.operatorCount << ",";

    // Per-operator timing
    json << "\"operatorTimings\":[";
    for (size_t i = 0; i < stats.operatorTimings.size(); ++i) {
        const auto& t = stats.operatorTimings[i];
        if (i > 0) json << ",";
        json << "{\"name\":\"" << jsonEscape(t.name) << "\",\"timeMs\":" << t.timeMs << "}";
    }
    json << "]}";

    std::string msg = json.str();

    // Broadcast to all clients
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto& client : m_impl->server.getClients()) {
        client->send(msg);
    }
}

void EditorBridge::sendSoloState(bool active, const std::string& operatorName) {
    if (!m_running || !m_impl) return;

    // Build JSON message
    std::ostringstream json;
    json << "{\"type\":\"solo_state\",\"active\":" << (active ? "true" : "false");
    if (active && !operatorName.empty()) {
        json << ",\"operator\":\"" << jsonEscape(operatorName) << "\"";
    }
    json << "}";

    std::string msg = json.str();

    // Broadcast to all clients
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto& client : m_impl->server.getClients()) {
        client->send(msg);
    }
}

} // namespace vivid
