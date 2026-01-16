/**
 * @file null_backend.cpp
 * @brief Null texture sharing backend for unsupported platforms (Linux)
 *
 * Provides a no-op implementation that gracefully fails all operations.
 * Linux does not have a standard texture sharing framework like Syphon/Spout.
 */

#if !defined(__APPLE__) && !defined(_WIN32)

#include <vivid/texshare/platform/share_backend.h>
#include <iostream>

namespace vivid::texshare {

/**
 * @brief Null backend for unsupported platforms
 */
class NullBackend : public ShareBackend {
public:
    NullBackend() {
        std::cout << "[TextureShare] Not supported on this platform (Linux)" << std::endl;
    }

    ~NullBackend() override = default;

    // Server (output)
    bool createServer(const std::string& name) override {
        std::cout << "[TextureShare] createServer: not supported on this platform" << std::endl;
        return false;
    }

    void publishTexture(WGPUTexture texture, int width, int height) override {
        // No-op
    }

    void destroyServer() override {
        // No-op
    }

    bool isServerActive() const override {
        return false;
    }

    // Client (input)
    bool connectToServer(const std::string& serverName) override {
        std::cout << "[TextureShare] connectToServer: not supported on this platform" << std::endl;
        return false;
    }

    bool hasNewFrame() override {
        return false;
    }

    WGPUTexture receiveTexture(WGPUDevice device) override {
        return nullptr;
    }

    void getTextureSize(int& width, int& height) const override {
        width = 0;
        height = 0;
    }

    void disconnect() override {
        // No-op
    }

    bool isConnected() const override {
        return false;
    }

    // Discovery
    std::vector<ServerInfo> listServers() override {
        return {};
    }
};

// =============================================================================
// Factory
// =============================================================================

std::unique_ptr<ShareBackend> ShareBackend::create() {
    return std::make_unique<NullBackend>();
}

} // namespace vivid::texshare

#endif // !__APPLE__ && !_WIN32
