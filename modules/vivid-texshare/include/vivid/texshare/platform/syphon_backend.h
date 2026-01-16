#pragma once

/**
 * @file syphon_backend.h
 * @brief Syphon texture sharing backend for macOS
 *
 * Syphon is a macOS framework for sharing GPU textures between applications.
 * It uses IOSurface for zero-copy texture sharing via Metal.
 *
 * @see https://syphon.github.io/
 */

#ifdef __APPLE__

#include <vivid/texshare/platform/share_backend.h>

// Forward declarations for Objective-C types
#ifdef __OBJC__
@class SyphonServer;
@class SyphonClient;
@class SyphonServerDirectory;
#else
typedef void SyphonServer;
typedef void SyphonClient;
typedef void SyphonServerDirectory;
#endif

namespace vivid::texshare {

/**
 * @brief Syphon implementation of ShareBackend for macOS
 *
 * Uses the Syphon framework to share textures with other applications.
 * Internally converts between WebGPU (Metal) textures and Syphon's
 * IOSurface-backed textures.
 */
class SyphonBackend : public ShareBackend {
public:
    SyphonBackend();
    ~SyphonBackend() override;

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
    // Objective-C objects (bridged)
    void* m_server = nullptr;   // SyphonMetalServer*
    void* m_client = nullptr;   // SyphonMetalClient*

    // Metal device for interop
    void* m_metalDevice = nullptr;   // id<MTLDevice>
    void* m_commandQueue = nullptr;  // id<MTLCommandQueue>

    // Received texture state
    WGPUTexture m_receivedTexture = nullptr;
    WGPUTextureView m_receivedView = nullptr;
    int m_receivedWidth = 0;
    int m_receivedHeight = 0;
    bool m_hasNewFrame = false;

    // Server state
    std::string m_serverName;

    // Helper methods
    void* getMetalDevice();
    void* getCommandQueue();
    void releaseReceivedTexture();
};

} // namespace vivid::texshare

#endif // __APPLE__
