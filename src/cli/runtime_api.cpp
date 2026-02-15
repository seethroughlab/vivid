// Windows: prevent min/max macros from conflicting with std::min/max
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <vivid/runtime_api.h>
#include <vivid/operator.h>
#include <ixwebsocket/IXHttpServer.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <mutex>
#include <chrono>
#include <fstream>
#include <sstream>
#include <filesystem>

using json = nlohmann::json;

namespace vivid {

// Helper to get MIME type from file extension
static std::string getMimeType(const std::string& path) {
    auto ext = std::filesystem::path(path).extension().string();
    if (ext == ".html") return "text/html";
    if (ext == ".css") return "text/css";
    if (ext == ".js") return "application/javascript";
    if (ext == ".json") return "application/json";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".woff") return "font/woff";
    if (ext == ".woff2") return "font/woff2";
    if (ext == ".ttf") return "font/ttf";
    return "application/octet-stream";
}

class RuntimeAPI::Impl {
public:
    ix::HttpServer server;
    std::mutex mutex;
    int port = 9876;
    std::string assetDir;  // Directory for serving static files

    Impl(int p) : server(p, "0.0.0.0"), port(p) {}
};

RuntimeAPI::RuntimeAPI() : m_impl(std::make_unique<Impl>(9876)) {}

RuntimeAPI::~RuntimeAPI() {
    stop();
}

void RuntimeAPI::start(int port) {
    if (m_running) return;

    m_port = port;
    m_impl = std::make_unique<Impl>(port);

    m_impl->server.setOnClientMessageCallback(
        [this](std::shared_ptr<ix::ConnectionState> state,
               ix::WebSocket& ws,
               const ix::WebSocketMessagePtr& msg) {

            if (msg->type == ix::WebSocketMessageType::Open) {
                std::cout << "[RuntimeAPI] Client connected from " << state->getRemoteIp() << "\n";
            }
            else if (msg->type == ix::WebSocketMessageType::Close) {
                std::cout << "[RuntimeAPI] Client disconnected\n";
            }
            else if (msg->type == ix::WebSocketMessageType::Message) {
                // Parse incoming command
                try {
                    json j = json::parse(msg->str);
                    std::string type = j.value("type", "");

                    if (type == "reload") {
                        std::cout << "[RuntimeAPI] Reload command received\n";
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
                            std::cout << "[RuntimeAPI] Param change: " << opName << "." << paramName << "\n";
                            if (m_paramChangeCallback) {
                                m_paramChangeCallback(opName, paramName, value);
                            }
                        }
                    }
                    else if (type == "solo_node") {
                        std::string opName = j.value("operator", "");
                        std::cout << "[RuntimeAPI] Solo node: " << opName << "\n";
                        if (m_soloNodeCallback) {
                            m_soloNodeCallback(opName);
                        }
                    }
                    else if (type == "solo_exit") {
                        std::cout << "[RuntimeAPI] Solo exit\n";
                        if (m_soloExitCallback) {
                            m_soloExitCallback();
                        }
                    }
                    else if (type == "select_node") {
                        std::string opName = j.value("operator", "");
                        std::cout << "[RuntimeAPI] Select node: " << opName << "\n";
                        if (m_selectNodeCallback) {
                            m_selectNodeCallback(opName);
                        }
                    }
                    else if (type == "focused_node") {
                        std::string opName = j.value("operator", "");
                        // Empty operator name means clear focus
                        if (opName.empty()) {
                            std::cout << "[RuntimeAPI] Clear focused node\n";
                        } else {
                            std::cout << "[RuntimeAPI] Focused node: " << opName << "\n";
                        }
                        if (m_focusedNodeCallback) {
                            m_focusedNodeCallback(opName);
                        }
                    }
                    else if (type == "request_operators") {
                        std::cout << "[RuntimeAPI] Operators requested\n";
                        if (m_requestOperatorsCallback) {
                            m_requestOperatorsCallback();
                        }
                    }
                    else if (type == "window_control") {
                        std::string setting = j.value("setting", "");
                        int value = j.value("value", 0);
                        std::cout << "[RuntimeAPI] Window control: " << setting << " = " << value << "\n";
                        if (m_windowControlCallback) {
                            m_windowControlCallback(setting, value);
                        }
                    }
                    else if (type == "request_window_state") {
                        std::cout << "[RuntimeAPI] Window state requested\n";
                        // The main loop will handle this by calling sendWindowState
                        if (m_requestOperatorsCallback) {
                            // Piggyback on operator request to trigger window state send
                            // (main.cpp handles both in the same callback area)
                            m_requestOperatorsCallback();
                        }
                    }
                    else if (type == "commit_changes") {
                        std::cout << "[RuntimeAPI] Commit pending changes\n";
                        commitPendingChanges();
                    }
                    else if (type == "discard_changes") {
                        std::cout << "[RuntimeAPI] Discard pending changes\n";
                        auto discarded = discardPendingChanges();
                        if (m_discardChangesCallback && !discarded.empty()) {
                            m_discardChangesCallback(discarded);
                        }
                    }
                    else if (type == "request_pending_changes") {
                        std::cout << "[RuntimeAPI] Pending changes requested\n";
                        sendPendingChanges();
                    }
                    else if (type == "request_compile_status") {
                        std::cout << "[RuntimeAPI] Compile status requested\n";
                        if (m_requestCompileStatusCallback) {
                            m_requestCompileStatusCallback();
                        }
                    }
                    else if (type == "capture_frame") {
                        std::string outputPath = j.value("outputPath", "/tmp/vivid_capture.png");
                        std::cout << "[RuntimeAPI] Capture frame requested: " << outputPath << "\n";
                        if (m_captureFrameCallback) {
                            m_captureFrameCallback(outputPath);
                        }
                    }
                    else if (type == "capture_audio") {
                        std::string outputPath = j.value("outputPath", "/tmp/vivid_capture.wav");
                        float duration = j.value("duration", 1.0f);
                        std::cout << "[RuntimeAPI] Capture audio requested: " << outputPath
                                  << " (" << duration << "s)\n";
                        if (m_captureAudioCallback) {
                            m_captureAudioCallback(outputPath, duration);
                        }
                    }
                    else if (type == "set_param_immediate") {
                        // Direct parameter set (MCP debugging) - apply immediately, no pending queue
                        std::string opName = j.value("operator", "");
                        std::string paramName = j.value("param", "");
                        float value[4] = {0, 0, 0, 0};

                        // Parse value (can be scalar or array)
                        if (j.contains("value")) {
                            if (j["value"].is_array()) {
                                const auto& arr = j["value"];
                                for (size_t i = 0; i < std::min(arr.size(), size_t(4)); ++i) {
                                    if (arr[i].is_number()) {
                                        value[i] = arr[i].get<float>();
                                    }
                                }
                            } else if (j["value"].is_number()) {
                                value[0] = j["value"].get<float>();
                            } else if (j["value"].is_boolean()) {
                                value[0] = j["value"].get<bool>() ? 1.0f : 0.0f;
                            }
                        }

                        std::cout << "[RuntimeAPI] Set param immediate: " << opName << "." << paramName << "\n";
                        bool success = false;
                        if (m_setParamImmediateCallback) {
                            success = m_setParamImmediateCallback(opName, paramName, value);
                        }
                        sendSetParamResult(opName, paramName, success);
                    }
                    else if (type == "advance_frames") {
                        int count = j.value("count", 1);
                        std::cout << "[RuntimeAPI] Advance frames requested: " << count << "\n";
                        requestFrameAdvance(count);
                        sendFrameAdvanceStarted(count);
                    }
                    else if (type == "request_chain_structure") {
                        std::cout << "[RuntimeAPI] Chain structure requested\n";
                        if (m_requestChainStructureCallback) {
                            auto operators = m_requestChainStructureCallback();
                            sendChainStructure(operators);
                        }
                    }
                    else if (type == "inspect_chain") {
                        bool perOp = j.value("per_operator_analysis", false);
                        std::cout << "[RuntimeAPI] Chain inspection requested"
                                  << (perOp ? " (per-operator analysis)" : "") << "\n";
                        if (m_inspectChainCallback) {
                            std::string jsonResult = m_inspectChainCallback(perOp);
                            sendInspectChain(jsonResult);
                        }
                    }
                    else if (type == "request_frame_info") {
                        std::cout << "[RuntimeAPI] Frame info requested\n";
                        if (m_requestFrameInfoCallback) {
                            auto info = m_requestFrameInfoCallback();
                            sendFrameInfo(info);
                        }
                    }
                    else if (type == "reset_time") {
                        std::cout << "[RuntimeAPI] Reset time requested\n";
                        if (m_resetTimeCallback) {
                            m_resetTimeCallback();
                            sendResetTimeComplete();
                        }
                    }
                    else if (type == "file_read") {
                        std::string path = j.value("path", "");
                        std::cout << "[RuntimeAPI] File read requested: " << path << "\n";
                        if (m_fileReadCallback && !path.empty()) {
                            std::string content = m_fileReadCallback(path);
                            sendFileContent(path, content);
                        }
                    }
                    else if (type == "file_write") {
                        std::string path = j.value("path", "");
                        std::string content = j.value("content", "");
                        std::cout << "[RuntimeAPI] File write requested: " << path << "\n";
                        if (m_fileWriteCallback && !path.empty()) {
                            bool success = m_fileWriteCallback(path, content);
                            sendFileWriteResult(path, success);
                        }
                    }
                    else if (type == "get_compile_status") {
                        // Alias for request_compile_status (used by IDE panel)
                        std::cout << "[RuntimeAPI] Compile status requested (get_compile_status)\n";
                        if (m_requestCompileStatusCallback) {
                            m_requestCompileStatusCallback();
                        }
                    }
                    else if (type == "pty_input") {
                        std::string data = j.value("data", "");
                        if (m_ptyInputCallback && !data.empty()) {
                            m_ptyInputCallback(data);
                        }
                    }
                    else if (type == "pty_resize") {
                        int cols = j.value("cols", 80);
                        int rows = j.value("rows", 24);
                        if (m_ptyResizeCallback) {
                            m_ptyResizeCallback(cols, rows);
                        }
                    }
                } catch (const json::exception& e) {
                    std::cerr << "[RuntimeAPI] JSON parse error: " << e.what() << "\n";
                }
            }
            else if (msg->type == ix::WebSocketMessageType::Error) {
                std::cerr << "[RuntimeAPI] Error: " << msg->errorInfo.reason << "\n";
            }
        }
    );

    // HTTP handler for serving static files (IDE panel)
    m_impl->server.setOnConnectionCallback(
        [this](ix::HttpRequestPtr request, std::shared_ptr<ix::ConnectionState> /*state*/)
        -> ix::HttpResponsePtr {
            // Check if this is a WebSocket upgrade request - let it through
            auto it = request->headers.find("Upgrade");
            if (it != request->headers.end() && it->second == "websocket") {
                return nullptr;  // Let WebSocket handler take over
            }

            // Handle HTTP GET requests for static files
            if (request->method == "GET") {
                std::string uri = request->uri;

                // Default to index.html for root
                if (uri == "/" || uri == "/ide" || uri == "/ide/") {
                    uri = "/index.html";
                }

                // Security: prevent directory traversal
                if (uri.find("..") != std::string::npos) {
                    return std::make_shared<ix::HttpResponse>(403, "Forbidden",
                        ix::HttpErrorCode::Ok, ix::WebSocketHttpHeaders(), "Forbidden");
                }

                // Build file path
                if (!m_impl->assetDir.empty()) {
                    std::string filePath = m_impl->assetDir + uri;

                    // Check if file exists
                    if (std::filesystem::exists(filePath) && std::filesystem::is_regular_file(filePath)) {
                        // Read file content
                        std::ifstream file(filePath, std::ios::binary);
                        if (file) {
                            std::stringstream buffer;
                            buffer << file.rdbuf();
                            std::string content = buffer.str();

                            ix::WebSocketHttpHeaders headers;
                            headers["Content-Type"] = getMimeType(filePath);
                            headers["Access-Control-Allow-Origin"] = "*";

                            return std::make_shared<ix::HttpResponse>(200, "OK",
                                ix::HttpErrorCode::Ok, headers, content);
                        }
                    }
                }

                // File not found
                return std::make_shared<ix::HttpResponse>(404, "Not Found",
                    ix::HttpErrorCode::Ok, ix::WebSocketHttpHeaders(), "Not Found");
            }

            // Method not allowed
            return std::make_shared<ix::HttpResponse>(405, "Method Not Allowed",
                ix::HttpErrorCode::Ok, ix::WebSocketHttpHeaders(), "Method Not Allowed");
        }
    );

    auto res = m_impl->server.listen();
    if (!res.first) {
        std::cerr << "[RuntimeAPI] Failed to start on port " << port << ": " << res.second << "\n";
        return;
    }

    m_impl->server.start();
    m_running = true;
    std::cout << "[RuntimeAPI] Listening on port " << port << "\n";
}

void RuntimeAPI::stop() {
    if (!m_running) return;

    m_impl->server.stop();
    m_running = false;
    std::cout << "[RuntimeAPI] Stopped\n";
}

void RuntimeAPI::setAssetDirectory(const std::string& dir) {
    if (m_impl) {
        m_impl->assetDir = dir;
        std::cout << "[RuntimeAPI] Serving static files from: " << dir << "\n";
    }
}

size_t RuntimeAPI::clientCount() const {
    if (!m_impl) return 0;
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->server.getClients().size();
}

void RuntimeAPI::sendCompileStatus(bool success, const std::string& message) {
    // Cache the status for late-connecting clients
    m_cachedCompileSuccess = success;
    m_cachedCompileMessage = message;

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

void RuntimeAPI::sendOperatorList(const std::vector<RuntimeOperatorInfo>& operators) {
    std::cout << "[RuntimeAPI] sendOperatorList called with " << operators.size() << " operators\n";
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
        if (!op.error.empty()) {
            opJson["error"] = op.error;
        }
        j["operators"].push_back(opJson);
    }

    std::string msg = j.dump();

    // Broadcast to all clients
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto& client : m_impl->server.getClients()) {
        client->send(msg);
    }
}

void RuntimeAPI::sendParamValues(const std::vector<RuntimeParamInfo>& params) {
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

void RuntimeAPI::sendPerformanceStats(const RuntimePerformanceStats& stats) {
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

void RuntimeAPI::sendSoloState(bool active, const std::string& operatorName) {
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

void RuntimeAPI::sendWindowState(const RuntimeWindowState& state) {
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

void RuntimeAPI::addPendingChange(const PendingChange& change) {
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
            std::cout << "[RuntimeAPI] Updated pending change: " << change.operatorName
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
    std::cout << "[RuntimeAPI] Added pending change: " << change.operatorName
              << "." << change.paramName << " (total: " << m_pendingChanges.size() << ")\n";
    sendPendingChanges();
}

void RuntimeAPI::sendPendingChanges() {
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

void RuntimeAPI::commitPendingChanges() {
    std::cout << "[RuntimeAPI] Committing " << m_pendingChanges.size() << " pending changes\n";
    m_pendingChanges.clear();
    sendPendingChanges();
}

std::vector<PendingChange> RuntimeAPI::discardPendingChanges() {
    std::cout << "[RuntimeAPI] Discarding " << m_pendingChanges.size() << " pending changes\n";
    std::vector<PendingChange> discarded = std::move(m_pendingChanges);
    m_pendingChanges.clear();
    sendPendingChanges();
    return discarded;
}

void RuntimeAPI::sendCaptureResult(bool success, const std::string& outputPath, const std::string& error) {
    if (!m_running || !m_impl) return;

    json j;
    j["type"] = "capture_result";
    j["success"] = success;
    j["outputPath"] = outputPath;
    if (!error.empty()) {
        j["error"] = error;
    }

    std::string msg = j.dump();

    // Broadcast to all clients
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto& client : m_impl->server.getClients()) {
        client->send(msg);
    }
}

void RuntimeAPI::sendCaptureAudioResult(bool success, const std::string& outputPath,
                                         const std::string& analysisJson, const std::string& error) {
    if (!m_running || !m_impl) return;

    json j;
    j["type"] = "capture_audio_result";
    j["success"] = success;
    j["outputPath"] = outputPath;
    if (!analysisJson.empty()) {
        j["analysis"] = json::parse(analysisJson, nullptr, false);
    }
    if (!error.empty()) {
        j["error"] = error;
    }

    std::string msg = j.dump();

    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto& client : m_impl->server.getClients()) {
        client->send(msg);
    }
}

// -------------------------------------------------------------------------
// Direct parameter control (MCP debugging tools)
// -------------------------------------------------------------------------

void RuntimeAPI::sendSetParamResult(const std::string& opName, const std::string& paramName, bool success) {
    if (!m_running || !m_impl) return;

    json j;
    j["type"] = "set_param_result";
    j["operator"] = opName;
    j["param"] = paramName;
    j["success"] = success;

    std::string msg = j.dump();

    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto& client : m_impl->server.getClients()) {
        client->send(msg);
    }
}

// -------------------------------------------------------------------------
// Frame advance control (MCP debugging tools)
// -------------------------------------------------------------------------

void RuntimeAPI::requestFrameAdvance(int count) {
    m_pendingFrameAdvance.store(count);
}

int RuntimeAPI::consumePendingFrameAdvance() {
    return m_pendingFrameAdvance.exchange(0);
}

void RuntimeAPI::sendFrameAdvanceStarted(int count) {
    if (!m_running || !m_impl) return;

    json j;
    j["type"] = "frame_advance_started";
    j["count"] = count;

    std::string msg = j.dump();

    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto& client : m_impl->server.getClients()) {
        client->send(msg);
    }
}

void RuntimeAPI::sendFrameAdvanceComplete(int newFrame) {
    if (!m_running || !m_impl) return;

    json j;
    j["type"] = "frame_advance_complete";
    j["frame"] = newFrame;

    std::string msg = j.dump();

    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto& client : m_impl->server.getClients()) {
        client->send(msg);
    }
}

// -------------------------------------------------------------------------
// Chain structure (MCP get_chain_structure tool)
// -------------------------------------------------------------------------

void RuntimeAPI::sendChainStructure(const std::vector<ChainOperatorInfo>& operators) {
    if (!m_running || !m_impl) return;

    json j;
    j["type"] = "chain_structure";
    j["operators"] = json::array();

    for (const auto& op : operators) {
        json opJson;
        opJson["name"] = op.name;
        opJson["displayName"] = op.displayName;
        opJson["outputType"] = op.outputType;
        opJson["inputs"] = op.inputs;
        j["operators"].push_back(opJson);
    }

    std::string msg = j.dump();

    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto& client : m_impl->server.getClients()) {
        client->send(msg);
    }
}

// -------------------------------------------------------------------------
// Chain introspection (MCP inspect_chain tool)
// -------------------------------------------------------------------------

void RuntimeAPI::sendInspectChain(const std::string& inspectionJson) {
    if (!m_running || !m_impl) return;

    // Wrap the inspection data in a typed message
    json j;
    j["type"] = "inspect_chain";
    // Parse the inspection JSON and embed it
    try {
        j["data"] = json::parse(inspectionJson);
    } catch (...) {
        j["data"] = inspectionJson;  // Fallback: send as string
    }

    std::string msg = j.dump();

    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto& client : m_impl->server.getClients()) {
        client->send(msg);
    }
}

// -------------------------------------------------------------------------
// Frame info (MCP get_frame_info tool)
// -------------------------------------------------------------------------

void RuntimeAPI::sendFrameInfo(const FrameInfo& info) {
    if (!m_running || !m_impl) return;

    json j;
    j["type"] = "frame_info";
    j["frame"] = info.frame;
    j["time"] = info.time;
    j["fps"] = info.fps;

    std::string msg = j.dump();

    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto& client : m_impl->server.getClients()) {
        client->send(msg);
    }
}

// -------------------------------------------------------------------------
// Reset time (MCP reset_time tool)
// -------------------------------------------------------------------------

void RuntimeAPI::sendResetTimeComplete() {
    if (!m_running || !m_impl) return;

    json j;
    j["type"] = "reset_time_complete";
    j["success"] = true;

    std::string msg = j.dump();

    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto& client : m_impl->server.getClients()) {
        client->send(msg);
    }
}

// -------------------------------------------------------------------------
// File operations (IDE panel)
// -------------------------------------------------------------------------

void RuntimeAPI::sendFileContent(const std::string& path, const std::string& content) {
    if (!m_running || !m_impl) return;

    json j;
    j["type"] = "file_content";
    j["path"] = path;
    j["content"] = content;

    std::string msg = j.dump();

    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto& client : m_impl->server.getClients()) {
        client->send(msg);
    }
}

void RuntimeAPI::sendFileWriteResult(const std::string& path, bool success, const std::string& error) {
    if (!m_running || !m_impl) return;

    json j;
    j["type"] = "file_write_result";
    j["path"] = path;
    j["success"] = success;
    if (!error.empty()) {
        j["error"] = error;
    }

    std::string msg = j.dump();

    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto& client : m_impl->server.getClients()) {
        client->send(msg);
    }
}

// -------------------------------------------------------------------------
// PTY operations (IDE panel terminal)
// -------------------------------------------------------------------------

void RuntimeAPI::sendPtyOutput(const std::string& data) {
    if (!m_running || !m_impl || data.empty()) return;

    json j;
    j["type"] = "pty_output";
    j["data"] = data;

    std::string msg = j.dump();

    std::lock_guard<std::mutex> lock(m_impl->mutex);
    for (auto& client : m_impl->server.getClients()) {
        client->send(msg);
    }
}

} // namespace vivid
