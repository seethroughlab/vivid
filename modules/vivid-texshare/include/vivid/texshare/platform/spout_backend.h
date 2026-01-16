#pragma once

/**
 * @file spout_backend.h
 * @brief Spout texture sharing backend for Windows
 *
 * Spout is a Windows framework for sharing GPU textures between applications.
 * It uses DirectX shared handles for zero-copy texture sharing.
 *
 * @see https://spout.zeal.co/
 */

#ifdef _WIN32

#include <vivid/texshare/platform/share_backend.h>

namespace vivid::texshare {

/**
 * @brief Spout implementation of ShareBackend for Windows
 *
 * Uses the Spout SDK to share textures with other applications.
 * Internally converts between WebGPU (DX12/Vulkan) textures and
 * Spout's DX11 shared textures.
 *
 * @note This is currently a stub implementation. Full Spout support
 * requires additional DirectX interop work.
 */
class SpoutBackend : public ShareBackend {
public:
    SpoutBackend();
    ~SpoutBackend() override;

    // Server (output)
    bool createServer(const std::string& name) override;
    void publishTexture(WGPUTexture texture, int width, int height) override;
    void destroyServer() override;
    bool isServerActive() const override;

    // Client (input)
    bool connectToServer(const std::string& serverName) override;
    bool hasNewFrame() override;
    WGPUTexture receiveTexture(WGPUDevice device) override;
    void getTextureSize(int& width, int& height) const override;
    void disconnect() override;
    bool isConnected() const override;

    // Discovery
    std::vector<ServerInfo> listServers() override;

private:
    // Spout sender/receiver handles
    void* m_spoutSender = nullptr;
    void* m_spoutReceiver = nullptr;

    std::string m_serverName;
    int m_width = 0;
    int m_height = 0;
    bool m_connected = false;
};

} // namespace vivid::texshare

#endif // _WIN32
