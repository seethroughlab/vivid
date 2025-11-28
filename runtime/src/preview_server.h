#pragma once
#include <ixwebsocket/IXWebSocketServer.h>
#include <nlohmann/json.hpp>
#include <vivid/types.h>
#include <vector>
#include <string>
#include <mutex>
#include <functional>

namespace vivid {

struct NodePreview {
    std::string id;
    int sourceLine = 0;
    OutputKind kind = OutputKind::Texture;
    std::string base64Image;   // For textures (JPEG base64) - legacy mode
    float value = 0.0f;        // For single values
    std::vector<float> values; // For value arrays
    int width = 0;             // Texture dimensions
    int height = 0;
};

// Lightweight metadata for shared memory mode
struct PreviewSlotInfo {
    std::string id;
    int slot;           // Index in shared memory
    int sourceLine;
    OutputKind kind;
    bool updated;       // True if changed this frame
};

class PreviewServer {
public:
    explicit PreviewServer(int port);
    ~PreviewServer();

    void start();
    void stop();
    bool isRunning() const { return running_; }

    // Send updates to all connected clients
    void sendNodeUpdates(const std::vector<NodePreview>& previews);  // Legacy: full image data
    void sendPreviewMetadata(const std::vector<PreviewSlotInfo>& slots, uint32_t frame,
                             const std::string& sharedMemName);  // New: metadata only
    void sendCompileStatus(bool success, const std::string& message);
    void sendError(const std::string& error);

    // Set callback for incoming commands
    using CommandCallback = std::function<void(const std::string& type, const nlohmann::json& data)>;
    void setCommandCallback(CommandCallback callback) { commandCallback_ = callback; }

    // Get number of connected clients
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
