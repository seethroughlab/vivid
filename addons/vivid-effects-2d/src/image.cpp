// Vivid Effects 2D - Image Operator Implementation

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <vivid/effects/image.h>
#include <vivid/context.h>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

namespace vivid::effects {

Image::~Image() {
    cleanup();
}

void Image::init(Context& ctx) {
    if (m_initialized && !m_needsReload) return;

    if (!m_filePath.empty()) {
        loadImage(ctx);
    }

    m_initialized = true;
}

void Image::loadImage(Context& ctx) {
    // Try to find the file
    fs::path path = m_filePath;

    // If not absolute, try relative to current directory and common locations
    if (!path.is_absolute()) {
        if (!fs::exists(path)) {
            // Try assets/images/
            fs::path assetsPath = fs::path("assets/images") / m_filePath;
            if (fs::exists(assetsPath)) {
                path = assetsPath;
            }
        }
    }

    if (!fs::exists(path)) {
        std::cerr << "Image: File not found: " << m_filePath << std::endl;
        return;
    }

    // Load image with stb_image
    int width, height, channels;
    unsigned char* data = stbi_load(path.string().c_str(), &width, &height, &channels, 4);  // Force RGBA

    if (!data) {
        std::cerr << "Image: Failed to load: " << path << " - " << stbi_failure_reason() << std::endl;
        return;
    }

    // Release old output if exists
    releaseOutput();

    // Store dimensions
    m_width = width;
    m_height = height;

    // Create GPU texture
    WGPUTextureDescriptor texDesc = {};
    texDesc.label = toStringView("Image Texture");
    texDesc.size.width = width;
    texDesc.size.height = height;
    texDesc.size.depthOrArrayLayers = 1;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;  // 8-bit for loaded images
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;

    m_output = wgpuDeviceCreateTexture(ctx.device(), &texDesc);

    // Create texture view
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;
    m_outputView = wgpuTextureCreateView(m_output, &viewDesc);

    // Upload pixel data
    WGPUTexelCopyTextureInfo destination = {};
    destination.texture = m_output;
    destination.mipLevel = 0;
    destination.origin = {0, 0, 0};
    destination.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout dataLayout = {};
    dataLayout.offset = 0;
    dataLayout.bytesPerRow = width * 4;
    dataLayout.rowsPerImage = height;

    WGPUExtent3D writeSize = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};

    wgpuQueueWriteTexture(ctx.queue(), &destination, data, width * height * 4, &dataLayout, &writeSize);

    // Free CPU image data
    stbi_image_free(data);

    m_needsReload = false;
    std::cout << "Image: Loaded " << path << " (" << width << "x" << height << ")" << std::endl;
}

void Image::process(Context& ctx) {
    if (!m_initialized || m_needsReload) {
        init(ctx);
    }
    // Image is static - no processing needed per frame
}

void Image::cleanup() {
    releaseOutput();
    m_initialized = false;
}

} // namespace vivid::effects
