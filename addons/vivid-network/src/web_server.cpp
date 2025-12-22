#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <vivid/network/web_server.h>
#include <vivid/context.h>
#include <vivid/chain.h>
#include <ixwebsocket/IXHttpServer.h>
#include <ixwebsocket/IXWebSocket.h>
#include <imgui.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace vivid::network {

WebServer::WebServer() = default;

WebServer::~WebServer() {
    cleanup();
}

void WebServer::port(int port) {
    m_port = port;
}

void WebServer::host(const std::string& host) {
    m_host = host;
}

void WebServer::staticDir(const std::string& path) {
    m_staticDir = path;
    // Ensure trailing slash
    if (!m_staticDir.empty() && m_staticDir.back() != '/') {
        m_staticDir += '/';
    }
}

void WebServer::route(const std::string& path, RouteHandler handler) {
    m_routes[path] = handler;
}

void WebServer::broadcast(const std::string& message) {
    std::lock_guard<std::mutex> lock(m_wsMutex);
    for (auto* ws : m_wsClients) {
        ws->send(message);
    }
}

void WebServer::broadcastJson(const std::string& type, const std::string& data) {
    std::string json = "{\"type\":\"" + type + "\",\"data\":" + data + "}";
    broadcast(json);
}

size_t WebServer::connectionCount() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(m_wsMutex));
    return m_wsClients.size();
}

void WebServer::init(Context& ctx) {
    m_ctx = &ctx;
    startServer();
}

void WebServer::process(Context& ctx) {
    // Periodically broadcast frame info to connected clients
    // (Could be made configurable)
}

void WebServer::cleanup() {
    stopServer();
    m_ctx = nullptr;
}

void WebServer::startServer() {
    if (m_running) return;

    m_server = std::make_unique<ix::HttpServer>(m_port, m_host);

    // Set up HTTP request handler
    m_server->setOnConnectionCallback(
        [this](ix::HttpRequestPtr request,
               std::shared_ptr<ix::ConnectionState> connectionState) -> ix::HttpResponsePtr {

            std::string response = handleRequest(request->method, request->uri, request->body);

            // Determine content type
            std::string contentType = "text/plain";
            if (response.find('{') == 0 || response.find('[') == 0) {
                contentType = "application/json";
            } else if (request->uri.find(".html") != std::string::npos ||
                       request->uri == "/" ||
                       response.find("<!DOCTYPE") == 0 ||
                       response.find("<html") == 0) {
                contentType = "text/html";
            } else if (request->uri.find(".css") != std::string::npos) {
                contentType = "text/css";
            } else if (request->uri.find(".js") != std::string::npos) {
                contentType = "application/javascript";
            }

            int statusCode = 200;
            if (response.find("\"error\"") != std::string::npos) {
                statusCode = 400;
            } else if (response == "Not Found") {
                statusCode = 404;
                contentType = "text/plain";
            }

            auto httpResponse = std::make_shared<ix::HttpResponse>(
                statusCode,
                statusCode == 200 ? "OK" : (statusCode == 404 ? "Not Found" : "Error"),
                ix::HttpErrorCode::Ok,
                ix::WebSocketHttpHeaders{{"Content-Type", contentType},
                                         {"Access-Control-Allow-Origin", "*"}},
                response
            );

            return httpResponse;
        }
    );

    // Set up WebSocket handler
    m_server->setOnClientMessageCallback(
        [this](std::shared_ptr<ix::ConnectionState> connectionState,
               ix::WebSocket& ws,
               const ix::WebSocketMessagePtr& msg) {

            if (msg->type == ix::WebSocketMessageType::Open) {
                std::lock_guard<std::mutex> lock(m_wsMutex);
                m_wsClients.insert(&ws);
                std::cout << "[WebServer] WebSocket client connected" << std::endl;
            }
            else if (msg->type == ix::WebSocketMessageType::Close) {
                std::lock_guard<std::mutex> lock(m_wsMutex);
                m_wsClients.erase(&ws);
                std::cout << "[WebServer] WebSocket client disconnected" << std::endl;
            }
            else if (msg->type == ix::WebSocketMessageType::Message) {
                // Handle incoming WebSocket messages
                // Could be parameter updates, commands, etc.
                std::string response = handleRequest("WS", "/ws", msg->str);
                if (!response.empty()) {
                    ws.send(response);
                }
            }
        }
    );

    // Start the server
    auto res = m_server->listen();
    if (!res.first) {
        std::cerr << "[WebServer] Failed to start: " << res.second << std::endl;
        return;
    }

    m_server->start();
    m_running = true;

    std::cout << "[WebServer] Running at http://" << m_host << ":" << m_port << std::endl;
    if (!m_staticDir.empty()) {
        std::cout << "[WebServer] Serving static files from: " << m_staticDir << std::endl;
    }
}

void WebServer::stopServer() {
    if (!m_running) return;

    if (m_server) {
        m_server->stop();
        m_server.reset();
    }

    {
        std::lock_guard<std::mutex> lock(m_wsMutex);
        m_wsClients.clear();
    }

    m_running = false;
    std::cout << "[WebServer] Stopped" << std::endl;
}

std::string WebServer::handleRequest(const std::string& method,
                                     const std::string& uri,
                                     const std::string& body) {
    // Check custom routes first
    for (const auto& [path, handler] : m_routes) {
        if (uri.find(path) == 0) {
            return handler(method, uri, body);
        }
    }

    // Built-in API routes
    if (uri.find("/api/") == 0) {
        if (uri == "/api/operators") {
            return handleApiOperators();
        }
        if (uri.find("/api/operator/") == 0) {
            std::string id = uri.substr(14);  // After "/api/operator/"
            return handleApiOperator(id, method, body);
        }
        if (uri == "/api/ping") {
            return "{\"status\":\"ok\"}";
        }
        return "{\"error\":\"Unknown API endpoint\"}";
    }

    // WebSocket messages
    if (method == "WS") {
        // Handle WebSocket message (JSON expected)
        // For now, just echo back
        return "";
    }

    // Static file serving
    if (!m_staticDir.empty()) {
        std::string path = uri == "/" ? "index.html" : uri.substr(1);  // Remove leading /
        return serveStaticFile(path);
    }

    return "Not Found";
}

std::string WebServer::serveStaticFile(const std::string& path) {
    std::string fullPath = m_staticDir + path;

    std::ifstream file(fullPath, std::ios::binary);
    if (!file) {
        return "Not Found";
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::string WebServer::getMimeType(const std::string& path) {
    if (path.find(".html") != std::string::npos) return "text/html";
    if (path.find(".css") != std::string::npos) return "text/css";
    if (path.find(".js") != std::string::npos) return "application/javascript";
    if (path.find(".json") != std::string::npos) return "application/json";
    if (path.find(".png") != std::string::npos) return "image/png";
    if (path.find(".jpg") != std::string::npos) return "image/jpeg";
    if (path.find(".svg") != std::string::npos) return "image/svg+xml";
    return "text/plain";
}

std::string WebServer::handleApiOperators() {
    if (!m_ctx) return "{\"error\":\"No context\"}";

    auto& chain = m_ctx->chain();
    std::string json = "[";
    bool first = true;

    for (const auto& name : chain.operatorNames()) {
        if (!first) json += ",";
        first = false;
        json += "{\"name\":\"" + name + "\"}";
    }

    json += "]";
    return json;
}

std::string WebServer::handleApiOperator(const std::string& id,
                                         const std::string& method,
                                         const std::string& body) {
    if (!m_ctx) return "{\"error\":\"No context\"}";

    auto& chain = m_ctx->chain();

    // Check if operator exists
    Operator* op = chain.getByName(id);
    if (!op) {
        return "{\"error\":\"Operator not found: " + id + "\"}";
    }

    if (method == "GET") {
        // Get operator info and parameters
        std::string json = "{\"name\":\"" + op->name() + "\"";

        // Add parameters if available
        auto params = op->params();
        if (!params.empty()) {
            json += ",\"params\":[";
            bool first = true;
            for (const auto& p : params) {
                if (!first) json += ",";
                first = false;

                float value[4] = {0};
                op->getParam(p.name, value);

                json += "{\"name\":\"" + p.name + "\"";
                json += ",\"value\":" + std::to_string(value[0]);
                json += ",\"min\":" + std::to_string(p.minVal);
                json += ",\"max\":" + std::to_string(p.maxVal);
                json += "}";
            }
            json += "]";
        }

        json += "}";
        return json;
    }
    else if (method == "POST") {
        // Set parameter (simple key=value parsing for now)
        // Expected body: {"param":"name","value":0.5}
        // This is a simple parser - in production use a JSON library

        size_t paramPos = body.find("\"param\"");
        size_t valuePos = body.find("\"value\"");

        if (paramPos != std::string::npos && valuePos != std::string::npos) {
            // Extract param name
            size_t nameStart = body.find(':', paramPos) + 1;
            size_t nameEnd = body.find(',', nameStart);
            if (nameEnd == std::string::npos) nameEnd = body.find('}', nameStart);

            std::string paramName = body.substr(nameStart, nameEnd - nameStart);
            // Remove quotes and whitespace
            paramName.erase(std::remove(paramName.begin(), paramName.end(), '"'), paramName.end());
            paramName.erase(std::remove(paramName.begin(), paramName.end(), ' '), paramName.end());

            // Extract value
            size_t valStart = body.find(':', valuePos) + 1;
            size_t valEnd = body.find_first_of(",}", valStart);
            std::string valueStr = body.substr(valStart, valEnd - valStart);
            valueStr.erase(std::remove(valueStr.begin(), valueStr.end(), ' '), valueStr.end());

            float value = std::stof(valueStr);
            float values[4] = {value, 0, 0, 0};

            if (op->setParam(paramName, values)) {
                // Broadcast parameter change to WebSocket clients
                broadcastJson("paramChange", "{\"operator\":\"" + id +
                              "\",\"param\":\"" + paramName +
                              "\",\"value\":" + std::to_string(value) + "}");
                return "{\"status\":\"ok\"}";
            }
            return "{\"error\":\"Failed to set parameter\"}";
        }
        return "{\"error\":\"Invalid request body\"}";
    }

    return "{\"error\":\"Method not supported\"}";
}

bool WebServer::drawVisualization(ImDrawList* dl, float minX, float minY, float maxX, float maxY) {
    float w = maxX - minX;
    float h = maxY - minY;
    float cx = minX + w * 0.5f;
    float cy = minY + h * 0.5f;
    float r = std::min(w, h) * 0.35f;

    // Background circle
    ImU32 bgColor = m_running ? IM_COL32(30, 60, 80, 255) : IM_COL32(60, 30, 30, 255);
    dl->AddCircleFilled(ImVec2(cx, cy), r, bgColor);
    dl->AddCircle(ImVec2(cx, cy), r, IM_COL32(100, 100, 100, 255), 32, 2.0f);

    // Web/globe icon using simple lines
    ImU32 iconColor = m_running ? IM_COL32(100, 200, 255, 255) : IM_COL32(180, 180, 180, 255);
    float iconR = r * 0.5f;

    // Horizontal lines
    dl->AddCircle(ImVec2(cx, cy), iconR, iconColor, 24, 1.5f);
    dl->AddLine(ImVec2(cx - iconR, cy), ImVec2(cx + iconR, cy), iconColor, 1.5f);
    dl->AddLine(ImVec2(cx - iconR * 0.85f, cy - iconR * 0.5f), ImVec2(cx + iconR * 0.85f, cy - iconR * 0.5f), iconColor, 1.0f);
    dl->AddLine(ImVec2(cx - iconR * 0.85f, cy + iconR * 0.5f), ImVec2(cx + iconR * 0.85f, cy + iconR * 0.5f), iconColor, 1.0f);

    // Vertical ellipse (simplified)
    dl->AddLine(ImVec2(cx, cy - iconR), ImVec2(cx, cy + iconR), iconColor, 1.5f);

    // Port number below
    char portStr[16];
    snprintf(portStr, sizeof(portStr), ":%d", m_port);
    ImVec2 portSize = ImGui::CalcTextSize(portStr);
    dl->AddText(ImVec2(cx - portSize.x * 0.5f, cy + r * 0.6f), IM_COL32(150, 150, 150, 255), portStr);

    // Connection count at bottom
    size_t clients = connectionCount();
    if (clients > 0) {
        char countStr[32];
        snprintf(countStr, sizeof(countStr), "%zu WS", clients);
        ImVec2 countSize = ImGui::CalcTextSize(countStr);
        dl->AddText(ImVec2(cx - countSize.x * 0.5f, maxY - countSize.y - 2), IM_COL32(100, 200, 255, 200), countStr);
    }

    return true;
}

} // namespace vivid::network
