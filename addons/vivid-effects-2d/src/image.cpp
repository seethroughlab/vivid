// Vivid Effects 2D - Image Operator Implementation

#include <vivid/effects/image.h>
#include <vivid/io/image_loader.h>
#include <vivid/context.h>
#include <iostream>

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
    // Load image via vivid-io
    auto imageData = vivid::io::loadImage(m_filePath);

    if (!imageData.valid()) {
        std::cerr << "Image: Failed to load: " << m_filePath << std::endl;
        return;
    }

    int width = imageData.width;
    int height = imageData.height;

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

    wgpuQueueWriteTexture(ctx.queue(), &destination, imageData.pixels.data(),
                          imageData.pixels.size(), &dataLayout, &writeSize);

    m_needsReload = false;
    std::cout << "Image: Loaded " << m_filePath << " (" << width << "x" << height << ")" << std::endl;
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
