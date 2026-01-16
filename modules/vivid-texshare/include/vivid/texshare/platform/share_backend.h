#pragma once

/**
 * @file share_backend.h
 * @brief Abstract interface for platform-specific texture sharing backends
 *
 * This interface abstracts the differences between Syphon (macOS), Spout (Windows),
 * and provides a common API for texture sharing between applications.
 */

#include <webgpu/webgpu.h>
#include <string>
#include <vector>
#include <memory>

namespace vivid::texshare {

/**
 * @brief Server info for discovered texture share servers
 */
struct ServerInfo {
    std::string name;       ///< Server name
    std::string appName;    ///< Application name (macOS Syphon only)

    bool operator==(const ServerInfo& other) const {
        return name == other.name && appName == other.appName;
    }
};

/**
 * @brief Abstract interface for texture sharing backends
 *
 * Platform implementations (Syphon, Spout) inherit from this class
 * to provide texture sharing functionality.
 */
class ShareBackend {
public:
    virtual ~ShareBackend() = default;

    // -------------------------------------------------------------------------
    /// @name Server (Output) - Publishing textures to other apps
    /// @{

    /**
     * @brief Create a texture share server
     * @param name Server name visible to other applications
     * @return true if server created successfully
     */
    virtual bool createServer(const std::string& name) = 0;

    /**
     * @brief Publish a texture to connected clients
     * @param texture WebGPU texture to share
     * @param width Texture width
     * @param height Texture height
     *
     * Called each frame to update the shared texture.
     */
    virtual void publishTexture(WGPUTexture texture, int width, int height) = 0;

    /**
     * @brief Destroy the server and stop sharing
     */
    virtual void destroyServer() = 0;

    /**
     * @brief Check if server is active
     * @return true if server is running
     */
    virtual bool isServerActive() const = 0;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Client (Input) - Receiving textures from other apps
    /// @{

    /**
     * @brief Connect to a texture share server
     * @param serverName Name of the server to connect to
     * @return true if connection initiated
     *
     * Connection may be asynchronous - use hasNewFrame() to check
     * when data is available.
     */
    virtual bool connectToServer(const std::string& serverName) = 0;

    /**
     * @brief Check if a new frame is available
     * @return true if new texture data is ready
     */
    virtual bool hasNewFrame() = 0;

    /**
     * @brief Receive texture from connected server
     * @param device WebGPU device for texture creation
     * @return Texture handle (borrowed - do not release)
     *
     * The returned texture is owned by the backend and will be
     * valid until the next call to receiveTexture() or disconnect().
     */
    virtual WGPUTexture receiveTexture(WGPUDevice device) = 0;

    /**
     * @brief Get the size of the received texture
     * @param[out] width Texture width
     * @param[out] height Texture height
     */
    virtual void getTextureSize(int& width, int& height) const = 0;

    /**
     * @brief Disconnect from the server
     */
    virtual void disconnect() = 0;

    /**
     * @brief Check if connected to a server
     * @return true if connected
     */
    virtual bool isConnected() const = 0;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Discovery
    /// @{

    /**
     * @brief List available texture share servers
     * @return Vector of server info for all discovered servers
     */
    virtual std::vector<ServerInfo> listServers() = 0;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Factory
    /// @{

    /**
     * @brief Create platform-appropriate backend
     * @return Unique pointer to backend instance
     *
     * Returns SyphonBackend on macOS, SpoutBackend on Windows,
     * NullBackend on unsupported platforms.
     */
    static std::unique_ptr<ShareBackend> create();

    /// @}
};

} // namespace vivid::texshare
