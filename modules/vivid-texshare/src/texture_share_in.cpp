// Vivid TextureShare - TextureShareIn Implementation

#include <vivid/texshare/texture_share_in.h>
#include <vivid/texshare/platform/share_backend.h>
#include <vivid/operator_registry.h>
#include <vivid/context.h>
#include <iostream>

namespace vivid::texshare {

REGISTER_OPERATOR(TextureShareIn, "I/O", "Receive textures from other applications via Syphon/Spout", false);

TextureShareIn::TextureShareIn() = default;

TextureShareIn::~TextureShareIn() {
    cleanup();
}

void TextureShareIn::init(Context& ctx) {
    m_device = ctx.device();

    // Create platform-specific backend
    m_backend = ShareBackend::create();

    if (!m_backend) {
        std::cerr << "[TextureShareIn] Failed to create texture sharing backend" << std::endl;
    }

    // Create fallback texture for when not connected
    createFallbackTexture(ctx);

    // Try to connect if server name is specified
    std::string name = serverName;
    if (!name.empty() && m_backend) {
        if (m_backend->connectToServer(name)) {
            m_currentServerName = name;
            std::cout << "[TextureShareIn] Connected to: " << name << std::endl;
        } else {
            std::cout << "[TextureShareIn] Server not found: " << name << std::endl;
        }
    }

    // Set initial output to fallback
    m_activeTexture = m_fallbackTexture;
    m_activeView = m_fallbackView;
}

void TextureShareIn::process(Context& ctx) {
    if (!m_backend) {
        return;
    }

    // Check if server name changed
    std::string newName = serverName;
    if (newName != m_currentServerName) {
        // Disconnect from old server
        if (!m_currentServerName.empty()) {
            m_backend->disconnect();
        }

        // Connect to new server
        m_currentServerName = newName;
        if (!newName.empty()) {
            if (m_backend->connectToServer(newName)) {
                std::cout << "[TextureShareIn] Connected to: " << newName << std::endl;
            } else {
                std::cout << "[TextureShareIn] Server not found: " << newName << std::endl;
            }
        }
    }

    // Check for new frames
    if (m_backend->isConnected() && m_backend->hasNewFrame()) {
        WGPUTexture receivedTex = m_backend->receiveTexture(ctx.device());
        if (receivedTex) {
            m_activeTexture = receivedTex;

            // Get size and update resolution
            int w, h;
            m_backend->getTextureSize(w, h);
            if (w > 0 && h > 0) {
                TextureOperator::setResolution(w, h);
            }

            // Create view from texture
            WGPUTextureViewDescriptor viewDesc = {};
            viewDesc.format = WGPUTextureFormat_BGRA8Unorm;  // Syphon/Spout use BGRA
            viewDesc.dimension = WGPUTextureViewDimension_2D;
            viewDesc.baseMipLevel = 0;
            viewDesc.mipLevelCount = 1;
            viewDesc.baseArrayLayer = 0;
            viewDesc.arrayLayerCount = 1;
            viewDesc.aspect = WGPUTextureAspect_All;

            // Note: The view is managed by the backend
            m_activeView = m_backend->receiveTexture(ctx.device()) ? wgpuTextureCreateView(m_activeTexture, &viewDesc) : nullptr;

            didCook();
        }
    } else if (!m_backend->isConnected()) {
        // Use fallback when not connected
        m_activeTexture = m_fallbackTexture;
        m_activeView = m_fallbackView;
    }
}

void TextureShareIn::cleanup() {
    if (m_backend) {
        m_backend->disconnect();
    }
    m_backend.reset();

    releaseFallbackTexture();

    m_activeTexture = nullptr;
    m_activeView = nullptr;
}

std::vector<ServerInfo> TextureShareIn::availableServers() const {
    if (!m_backend) {
        return {};
    }
    return m_backend->listServers();
}

bool TextureShareIn::isConnected() const {
    return m_backend && m_backend->isConnected();
}

void TextureShareIn::getReceivedSize(int& width, int& height) const {
    if (m_backend && m_backend->isConnected()) {
        m_backend->getTextureSize(width, height);
    } else {
        width = m_width;
        height = m_height;
    }
}

void TextureShareIn::createFallbackTexture(Context& ctx) {
    // Create a small black texture as fallback
    WGPUTextureDescriptor desc = {};
    desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    desc.dimension = WGPUTextureDimension_2D;
    desc.size.width = 64;
    desc.size.height = 64;
    desc.size.depthOrArrayLayers = 1;
    desc.format = WGPUTextureFormat_RGBA8Unorm;
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;

    m_fallbackTexture = wgpuDeviceCreateTexture(ctx.device(), &desc);

    // Create view
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;

    m_fallbackView = wgpuTextureCreateView(m_fallbackTexture, &viewDesc);

    // Fill with black
    std::vector<uint8_t> blackPixels(64 * 64 * 4, 0);

    WGPUExtent3D extent = {64, 64, 1};
    WGPUTexelCopyBufferLayout layout = {};
    layout.offset = 0;
    layout.bytesPerRow = 64 * 4;
    layout.rowsPerImage = 64;

    WGPUTexelCopyTextureInfo destInfo = {};
    destInfo.texture = m_fallbackTexture;
    destInfo.mipLevel = 0;
    destInfo.origin = {0, 0, 0};
    destInfo.aspect = WGPUTextureAspect_All;

    wgpuQueueWriteTexture(
        wgpuDeviceGetQueue(ctx.device()),
        &destInfo,
        blackPixels.data(),
        blackPixels.size(),
        &layout,
        &extent
    );

    // Set default resolution
    TextureOperator::setResolution(64, 64);
}

void TextureShareIn::releaseFallbackTexture() {
    if (m_fallbackView) {
        wgpuTextureViewRelease(m_fallbackView);
        m_fallbackView = nullptr;
    }
    if (m_fallbackTexture) {
        wgpuTextureRelease(m_fallbackTexture);
        m_fallbackTexture = nullptr;
    }
}

} // namespace vivid::texshare
