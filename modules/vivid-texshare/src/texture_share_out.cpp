// Vivid TextureShare - TextureShareOut Implementation

#include <vivid/texshare/texture_share_out.h>
#include <vivid/texshare/platform/share_backend.h>
#include <vivid/operator_registry.h>
#include <vivid/context.h>
#include <iostream>

namespace vivid::texshare {

REGISTER_OPERATOR(TextureShareOut, "I/O", "Share textures with other applications via Syphon/Spout", true);

TextureShareOut::TextureShareOut() = default;

TextureShareOut::~TextureShareOut() {
    cleanup();
}

void TextureShareOut::init(Context& ctx) {
    // Create platform-specific backend
    m_backend = ShareBackend::create();

    if (!m_backend) {
        std::cerr << "[TextureShareOut] Failed to create texture sharing backend" << std::endl;
        return;
    }

    // Start server with configured name
    m_currentServerName = serverName;
    if (m_backend->createServer(m_currentServerName)) {
        m_serverStarted = true;
        std::cout << "[TextureShareOut] Server started: " << m_currentServerName << std::endl;
    } else {
        std::cerr << "[TextureShareOut] Failed to start server: " << m_currentServerName << std::endl;
    }
}

void TextureShareOut::process(Context& ctx) {
    if (!m_backend) {
        return;
    }

    // Check if server name changed
    std::string newName = serverName;
    if (newName != m_currentServerName && m_serverStarted) {
        // Restart server with new name
        m_backend->destroyServer();
        m_currentServerName = newName;
        if (m_backend->createServer(m_currentServerName)) {
            std::cout << "[TextureShareOut] Server renamed to: " << m_currentServerName << std::endl;
        }
    }

    // Get input texture to share
    WGPUTextureView view = inputView(0);
    if (!view) {
        return;  // No input connected
    }

    // Get input operator for texture dimensions
    Operator* inputOp = getInput(0);
    if (!inputOp) {
        return;
    }

    WGPUTexture inputTex = inputOp->outputTexture();
    if (!inputTex) {
        return;
    }

    // Get texture dimensions from input
    int width = 0, height = 0;
    if (auto* texOp = dynamic_cast<effects::TextureOperator*>(inputOp)) {
        width = texOp->outputWidth();
        height = texOp->outputHeight();
    }

    if (width > 0 && height > 0) {
        // Publish the texture
        m_backend->publishTexture(inputTex, width, height);
    }
}

void TextureShareOut::cleanup() {
    if (m_backend && m_serverStarted) {
        m_backend->destroyServer();
        m_serverStarted = false;
        std::cout << "[TextureShareOut] Server stopped: " << m_currentServerName << std::endl;
    }
    m_backend.reset();
}

WGPUTextureView TextureShareOut::outputView() const {
    // Pass-through: return input view
    return inputView(0);
}

WGPUTexture TextureShareOut::outputTexture() const {
    // Pass-through: return input texture
    Operator* inputOp = getInput(0);
    return inputOp ? inputOp->outputTexture() : nullptr;
}

bool TextureShareOut::isSharing() const {
    return m_backend && m_backend->isServerActive();
}

} // namespace vivid::texshare
