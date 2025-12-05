#pragma once

#include <ixwebsocket/IXWebSocketServer.h>
#include <nlohmann/json.hpp>
#include "operator.h"
#include <vector>
#include <string>
#include <mutex>
#include <functional>

namespace vivid {

/// Preview data for an operator node
struct NodePreview {
    std::string id;
    int sourceLine = 0;
    OutputKind kind = OutputKind::Texture;
    std::string base64Image;   // For textures (JPEG base64)
    float value = 0.0f;        // For single values
    std::vector<float> values; // For value arrays
    int width = 0;
    int height = 0;
};

/// WebSocket server for VS Code extension communication
class PreviewServer {
public:
    explicit PreviewServer(int port = 9876);
    ~PreviewServer();

    void start();
    void stop();
    bool isRunning() const { return running_; }

    /// Send operator preview updates to all connected clients
    void sendNodeUpdates(const std::vector<NodePreview>& previews);

    /// Send compilation status
    void sendCompileStatus(bool success, const std::string& message);

    /// Send error message
    void sendError(const std::string& error);

    /// Callback for incoming commands from extension
    using CommandCallback = std::function<void(const std::string& type, const nlohmann::json& data)>;
    void setCommandCallback(CommandCallback callback) { commandCallback_ = callback; }

    /// Get number of connected clients
    size_t clientCount();

private:
    void onMessage(std::shared_ptr<ix::ConnectionState> state,
                   ix::WebSocket& ws,
                   const ix::WebSocketMessagePtr& msg);
    void broadcast(const std::string& message);

    ix::WebSocketServer server_;
    std::mutex mutex_;
    bool running_ = false;
    CommandCallback commandCallback_;
    int port_;
};

} // namespace vivid
