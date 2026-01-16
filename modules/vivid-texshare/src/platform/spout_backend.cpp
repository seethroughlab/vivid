/**
 * @file spout_backend.cpp
 * @brief Spout texture sharing backend for Windows (stub implementation)
 *
 * This is a stub implementation. Full Spout support requires:
 * - Spout SDK integration
 * - DirectX 11 texture interop with WebGPU (DX12/Vulkan)
 *
 * @see https://spout.zeal.co/
 */

#ifdef _WIN32

#include <vivid/texshare/platform/spout_backend.h>
#include <iostream>

namespace vivid::texshare {

SpoutBackend::SpoutBackend() {
    std::cout << "[SpoutBackend] Spout support not yet implemented" << std::endl;
}

SpoutBackend::~SpoutBackend() {
    destroyServer();
    disconnect();
}

// =============================================================================
// Server (Output)
// =============================================================================

bool SpoutBackend::createServer(const std::string& name) {
    std::cout << "[SpoutBackend] createServer: stub - not implemented" << std::endl;
    m_serverName = name;
    return false;
}

void SpoutBackend::publishTexture(WGPUTexture texture, int width, int height) {
    // Stub - no-op
}

void SpoutBackend::destroyServer() {
    // Stub - no-op
}

bool SpoutBackend::isServerActive() const {
    return false;
}

// =============================================================================
// Client (Input)
// =============================================================================

bool SpoutBackend::connectToServer(const std::string& serverName) {
    std::cout << "[SpoutBackend] connectToServer: stub - not implemented" << std::endl;
    return false;
}

bool SpoutBackend::hasNewFrame() {
    return false;
}

WGPUTexture SpoutBackend::receiveTexture(WGPUDevice device) {
    return nullptr;
}

void SpoutBackend::getTextureSize(int& width, int& height) const {
    width = m_width;
    height = m_height;
}

void SpoutBackend::disconnect() {
    m_connected = false;
}

bool SpoutBackend::isConnected() const {
    return m_connected;
}

// =============================================================================
// Discovery
// =============================================================================

std::vector<ServerInfo> SpoutBackend::listServers() {
    // Stub - return empty list
    return {};
}

// =============================================================================
// Factory
// =============================================================================

std::unique_ptr<ShareBackend> ShareBackend::create() {
    return std::make_unique<SpoutBackend>();
}

} // namespace vivid::texshare

#endif // _WIN32
