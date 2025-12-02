#pragma once

#include <vivid/vivid.h>
#include <string>
#include <vector>
#include <memory>

namespace vivid {
namespace syphon {

/**
 * @brief Information about an available Syphon server.
 */
struct ServerInfo {
    std::string name;       ///< Server name
    std::string appName;    ///< Application name
    std::string uuid;       ///< Unique identifier
};

/**
 * @brief Syphon server for sharing textures with other applications.
 *
 * Usage:
 * @code
 * syphon::Server server("My Vivid Output");
 * server.publishFrame(texture, ctx);
 * @endcode
 */
class Server {
public:
    /**
     * @brief Create a Syphon server.
     * @param name Server name (visible to other apps).
     */
    explicit Server(const std::string& name = "Vivid");

    ~Server();

    // Non-copyable
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Movable
    Server(Server&& other) noexcept;
    Server& operator=(Server&& other) noexcept;

    /**
     * @brief Check if server is valid and running.
     */
    bool valid() const { return impl_ != nullptr; }

    /**
     * @brief Get the server name.
     */
    const std::string& name() const { return name_; }

    /**
     * @brief Publish a frame to connected clients.
     * @param texture The texture to share.
     * @param ctx Context for GPU operations.
     *
     * This reads back the texture from GPU and publishes it via Syphon.
     * Call this once per frame with your final output.
     */
    void publishFrame(const Texture& texture, Context& ctx);

    /**
     * @brief Check if any clients are connected.
     */
    bool hasClients() const;

private:
    std::string name_;
    void* impl_ = nullptr;  // Opaque pointer to ObjC implementation
};

/**
 * @brief Syphon client for receiving textures from other applications.
 *
 * Usage:
 * @code
 * auto servers = syphon::Client::listServers();
 * syphon::Client client(servers[0]);
 * if (client.hasNewFrame()) {
 *     client.receiveFrame(texture, ctx);
 * }
 * @endcode
 */
class Client {
public:
    /**
     * @brief Create a disconnected client.
     */
    Client();

    /**
     * @brief Create a client connected to a specific server.
     * @param server Server info from listServers().
     */
    explicit Client(const ServerInfo& server);

    /**
     * @brief Create a client by server/app name.
     * @param serverName Server name (empty for any).
     * @param appName Application name (empty for any).
     */
    Client(const std::string& serverName, const std::string& appName = "");

    ~Client();

    // Non-copyable
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    // Movable
    Client(Client&& other) noexcept;
    Client& operator=(Client&& other) noexcept;

    /**
     * @brief Check if client is connected.
     */
    bool connected() const;

    /**
     * @brief Check if a new frame is available.
     */
    bool hasNewFrame() const;

    /**
     * @brief Receive the latest frame into a texture.
     * @param texture Output texture (will be resized if needed).
     * @param ctx Context for GPU operations.
     * @return true if a frame was received.
     */
    bool receiveFrame(Texture& texture, Context& ctx);

    /**
     * @brief Get the frame dimensions.
     * @param width Output width.
     * @param height Output height.
     */
    void getFrameSize(int& width, int& height) const;

    /**
     * @brief List all available Syphon servers.
     */
    static std::vector<ServerInfo> listServers();

    /**
     * @brief Print available servers to stdout.
     */
    static void printServers();

private:
    void* impl_ = nullptr;  // Opaque pointer to ObjC implementation
};

} // namespace syphon
} // namespace vivid
