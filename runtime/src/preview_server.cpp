#include "preview_server.h"
#include <iostream>

namespace vivid {

PreviewServer::PreviewServer(int port)
    : server_(port, "0.0.0.0")
    , port_(port) {

    server_.setOnClientMessageCallback(
        [this](std::shared_ptr<ix::ConnectionState> state,
               ix::WebSocket& ws,
               const ix::WebSocketMessagePtr& msg) {
            onMessage(state, ws, msg);
        }
    );
}

PreviewServer::~PreviewServer() {
    stop();
}

void PreviewServer::start() {
    auto res = server_.listen();
    if (!res.first) {
        std::cerr << "[PreviewServer] Failed to start: " << res.second << "\n";
        return;
    }
    server_.start();
    running_ = true;
    std::cout << "[PreviewServer] Listening on port " << port_ << "\n";
}

void PreviewServer::stop() {
    if (running_) {
        server_.stop();
        running_ = false;
        std::cout << "[PreviewServer] Stopped\n";
    }
}

size_t PreviewServer::clientCount() {
    std::lock_guard<std::mutex> lock(mutex_);
    return server_.getClients().size();
}

void PreviewServer::sendNodeUpdates(const std::vector<NodePreview>& previews) {
    if (!running_) return;

    nlohmann::json msg;
    msg["type"] = "node_update";
    msg["nodes"] = nlohmann::json::array();

    for (const auto& preview : previews) {
        nlohmann::json node;
        node["id"] = preview.id;
        node["line"] = preview.sourceLine;

        switch (preview.kind) {
            case OutputKind::Texture:
                node["kind"] = "texture";
                if (!preview.base64Image.empty()) {
                    node["preview"] = preview.base64Image;
                }
                if (preview.width > 0 && preview.height > 0) {
                    node["width"] = preview.width;
                    node["height"] = preview.height;
                }
                break;

            case OutputKind::Value:
                node["kind"] = "value";
                node["value"] = preview.value;
                break;

            case OutputKind::ValueArray:
                node["kind"] = "value_array";
                node["values"] = preview.values;
                break;

            case OutputKind::Geometry:
                node["kind"] = "geometry";
                break;
        }

        msg["nodes"].push_back(node);
    }

    broadcast(msg.dump());
}

void PreviewServer::sendCompileStatus(bool success, const std::string& message) {
    if (!running_) return;

    nlohmann::json msg;
    msg["type"] = "compile_status";
    msg["success"] = success;
    msg["message"] = message;

    broadcast(msg.dump());
}

void PreviewServer::sendError(const std::string& error) {
    if (!running_) return;

    nlohmann::json msg;
    msg["type"] = "error";
    msg["message"] = error;

    broadcast(msg.dump());
}

void PreviewServer::onMessage(std::shared_ptr<ix::ConnectionState> state,
                               ix::WebSocket& ws,
                               const ix::WebSocketMessagePtr& msg) {
    if (msg->type == ix::WebSocketMessageType::Open) {
        std::cout << "[PreviewServer] Client connected from " << state->getRemoteIp() << "\n";
    }
    else if (msg->type == ix::WebSocketMessageType::Close) {
        std::cout << "[PreviewServer] Client disconnected\n";
    }
    else if (msg->type == ix::WebSocketMessageType::Message) {
        try {
            auto json = nlohmann::json::parse(msg->str);
            std::string type = json.value("type", "");

            if (commandCallback_) {
                commandCallback_(type, json);
            }
        }
        catch (const std::exception& e) {
            std::cerr << "[PreviewServer] Parse error: " << e.what() << "\n";
        }
    }
    else if (msg->type == ix::WebSocketMessageType::Error) {
        std::cerr << "[PreviewServer] Error: " << msg->errorInfo.reason << "\n";
    }
}

void PreviewServer::broadcast(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& client : server_.getClients()) {
        client->send(message);
    }
}

} // namespace vivid
