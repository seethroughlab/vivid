#pragma once

/**
 * @file texture_share_in.h
 * @brief Texture input operator for receiving from other applications
 *
 * Uses Syphon on macOS, Spout on Windows to receive textures
 * from other applications (VJ software, media servers, etc.)
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/operator_registry.h>
#include <vivid/texshare/export.h>
#include <vivid/texshare/platform/share_backend.h>
#include <memory>
#include <string>
#include <vector>

namespace vivid::texshare {

class ShareBackend;  // Forward declaration

/**
 * @brief Receive textures from other applications
 *
 * TextureShareIn connects to a named texture server published by
 * another application and outputs the received texture. On macOS
 * it uses Syphon, on Windows it uses Spout.
 *
 * Example:
 * @code
 * auto& recv = chain.add<TextureShareIn>("recv");
 * recv.serverName = "Resolume Arena";
 *
 * auto& blur = chain.add<Blur>("blur");
 * blur.input("recv");
 *
 * chain.output("blur");
 * @endcode
 *
 * @par Server Discovery
 * Use availableServers() to get a list of servers:
 * @code
 * auto servers = recv.availableServers();
 * for (const auto& server : servers) {
 *     std::cout << server.name << " from " << server.appName << std::endl;
 * }
 * @endcode
 *
 * @par Platform Support
 * - macOS: Syphon - receives from Resolume, VDMX, MadMapper, etc.
 * - Windows: Spout - receives from Resolume, TouchDesigner, etc.
 * - Linux: No-op (outputs black texture)
 */
class VIVID_TEXSHARE_API TextureShareIn : public vivid::effects::TextureOperator {
public:
    TextureShareIn();
    ~TextureShareIn() override;

    // Non-copyable
    TextureShareIn(const TextureShareIn&) = delete;
    TextureShareIn& operator=(const TextureShareIn&) = delete;

    /// @brief Name of server to connect to (empty = no connection)
    std::string serverName;

    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "TextureShareIn"; }

    /**
     * @brief Returns borrowed texture view from share backend
     *
     * The texture is owned by the backend - do not release.
     */
    WGPUTextureView outputView() const override { return m_activeView; }

    /**
     * @brief Returns borrowed texture from share backend
     */
    WGPUTexture outputTexture() const override { return m_activeTexture; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Discovery & Status
    /// @{

    /**
     * @brief List available texture share servers
     * @return Vector of server info
     *
     * Call this to discover which applications are sharing textures.
     * The list is refreshed each time this method is called.
     */
    std::vector<ServerInfo> availableServers() const;

    /**
     * @brief Check if connected to a server
     * @return true if receiving textures
     */
    bool isConnected() const;

    /**
     * @brief Get the size of the received texture
     * @param[out] width Texture width
     * @param[out] height Texture height
     */
    void getReceivedSize(int& width, int& height) const;

    /// @}

private:
    std::unique_ptr<ShareBackend> m_backend;
    std::string m_currentServerName;

    // Borrowed texture from backend (follows Webcam pattern)
    WGPUTexture m_activeTexture = nullptr;
    WGPUTextureView m_activeView = nullptr;

    // Fallback texture for when not connected
    WGPUTexture m_fallbackTexture = nullptr;
    WGPUTextureView m_fallbackView = nullptr;
    WGPUDevice m_device = nullptr;

    void createFallbackTexture(Context& ctx);
    void releaseFallbackTexture();
};

} // namespace vivid::texshare
