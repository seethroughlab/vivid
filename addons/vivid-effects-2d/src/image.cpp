// Vivid Effects 2D - Image Operator Implementation

#include <vivid/effects/image.h>
#include <vivid/io/image_loader.h>
#include <vivid/context.h>
#include <iostream>
#include <cstring>

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

    // Create GPU texture using EFFECTS_FORMAT for compatibility with other operators
    WGPUTextureDescriptor texDesc = {};
    texDesc.label = toStringView("Image Texture");
    texDesc.size.width = width;
    texDesc.size.height = height;
    texDesc.size.depthOrArrayLayers = 1;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = EFFECTS_FORMAT;  // RGBA16Float for compatibility
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc;

    m_output = wgpuDeviceCreateTexture(ctx.device(), &texDesc);

    // Create texture view
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = EFFECTS_FORMAT;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;
    m_outputView = wgpuTextureCreateView(m_output, &viewDesc);

    // Convert 8-bit RGBA to 16-bit float RGBA
    std::vector<uint16_t> floatPixels(width * height * 4);
    for (size_t i = 0; i < imageData.pixels.size(); ++i) {
        // Convert uint8 [0-255] to float16 [0.0-1.0]
        float normalized = imageData.pixels[i] / 255.0f;
        // Convert float32 to float16 using simple bit manipulation
        // This is a basic conversion - for values in [0,1] range it works well
        uint32_t f32;
        std::memcpy(&f32, &normalized, sizeof(float));
        uint32_t sign = (f32 >> 16) & 0x8000;
        uint32_t exp = ((f32 >> 23) & 0xFF) - 127 + 15;
        uint32_t mant = (f32 >> 13) & 0x3FF;
        if (exp <= 0) {
            floatPixels[i] = sign;  // Denormalized or zero
        } else if (exp >= 31) {
            floatPixels[i] = sign | 0x7C00;  // Infinity
        } else {
            floatPixels[i] = sign | (exp << 10) | mant;
        }
    }

    // Upload pixel data
    WGPUTexelCopyTextureInfo destination = {};
    destination.texture = m_output;
    destination.mipLevel = 0;
    destination.origin = {0, 0, 0};
    destination.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout dataLayout = {};
    dataLayout.offset = 0;
    dataLayout.bytesPerRow = width * 4 * sizeof(uint16_t);  // 8 bytes per pixel (RGBA16Float)
    dataLayout.rowsPerImage = height;

    WGPUExtent3D writeSize = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};

    wgpuQueueWriteTexture(ctx.queue(), &destination, floatPixels.data(),
                          floatPixels.size() * sizeof(uint16_t), &dataLayout, &writeSize);

    // Lock resolution to prevent checkResize() from overwriting the loaded image
    m_resolutionLocked = true;

    m_needsReload = false;
    std::cout << "Image: Loaded " << m_filePath << " (" << width << "x" << height << ")" << std::endl;
}

void Image::process(Context& ctx) {
    if (!m_initialized || m_needsReload) {
        init(ctx);
    }
    // Image uses loaded file dimensions - no auto-resize

    if (!needsCook()) return;

    // Image is static - just mark as cooked
    didCook();
}

void Image::cleanup() {
    releaseOutput();
    m_initialized = false;
}

} // namespace vivid::effects
