// TexturedMaterial - PBR material with texture map support

#include <vivid/render3d/textured_material.h>
#include <vivid/io/image_loader.h>  // Full definition for io::ImageData
#include <vivid/effects/texture_operator.h>  // for toStringView
#include <vivid/context.h>
#include <iostream>

using vivid::effects::toStringView;

namespace vivid::render3d {

TexturedMaterial::TexturedMaterial() = default;

TexturedMaterial::~TexturedMaterial() {
    cleanup();
}

// -------------------------------------------------------------------------
// Texture Path Setters
// -------------------------------------------------------------------------

TexturedMaterial& TexturedMaterial::baseColor(const std::string& path) {
    m_baseColor.path = path;
    m_baseColor.needsLoad = true;
    return *this;
}

TexturedMaterial& TexturedMaterial::normal(const std::string& path) {
    m_normal.path = path;
    m_normal.needsLoad = true;
    return *this;
}

TexturedMaterial& TexturedMaterial::metallic(const std::string& path) {
    m_metallic.path = path;
    m_metallic.needsLoad = true;
    return *this;
}

TexturedMaterial& TexturedMaterial::roughness(const std::string& path) {
    m_roughness.path = path;
    m_roughness.needsLoad = true;
    return *this;
}

TexturedMaterial& TexturedMaterial::ao(const std::string& path) {
    m_ao.path = path;
    m_ao.needsLoad = true;
    return *this;
}

TexturedMaterial& TexturedMaterial::emissive(const std::string& path) {
    m_emissive.path = path;
    m_emissive.needsLoad = true;
    return *this;
}

// -------------------------------------------------------------------------
// From-Data Setters (for embedded GLTF textures)
// -------------------------------------------------------------------------

TexturedMaterial& TexturedMaterial::baseColorFromData(const io::ImageData& data) {
    m_baseColor.pendingPixels = data.pixels;
    m_baseColor.pendingWidth = data.width;
    m_baseColor.pendingHeight = data.height;
    m_baseColor.hasData = true;
    m_baseColor.needsLoad = true;
    return *this;
}

TexturedMaterial& TexturedMaterial::normalFromData(const io::ImageData& data) {
    m_normal.pendingPixels = data.pixels;
    m_normal.pendingWidth = data.width;
    m_normal.pendingHeight = data.height;
    m_normal.hasData = true;
    m_normal.needsLoad = true;
    return *this;
}

TexturedMaterial& TexturedMaterial::metallicFromData(const io::ImageData& data) {
    m_metallic.pendingPixels = data.pixels;
    m_metallic.pendingWidth = data.width;
    m_metallic.pendingHeight = data.height;
    m_metallic.hasData = true;
    m_metallic.needsLoad = true;
    return *this;
}

TexturedMaterial& TexturedMaterial::roughnessFromData(const io::ImageData& data) {
    m_roughness.pendingPixels = data.pixels;
    m_roughness.pendingWidth = data.width;
    m_roughness.pendingHeight = data.height;
    m_roughness.hasData = true;
    m_roughness.needsLoad = true;
    return *this;
}

TexturedMaterial& TexturedMaterial::aoFromData(const io::ImageData& data) {
    m_ao.pendingPixels = data.pixels;
    m_ao.pendingWidth = data.width;
    m_ao.pendingHeight = data.height;
    m_ao.hasData = true;
    m_ao.needsLoad = true;
    return *this;
}

TexturedMaterial& TexturedMaterial::emissiveFromData(const io::ImageData& data) {
    m_emissive.pendingPixels = data.pixels;
    m_emissive.pendingWidth = data.width;
    m_emissive.pendingHeight = data.height;
    m_emissive.hasData = true;
    m_emissive.needsLoad = true;
    return *this;
}

// -------------------------------------------------------------------------
// Fallback Value Setters
// -------------------------------------------------------------------------

TexturedMaterial& TexturedMaterial::baseColorFactor(float r, float g, float b, float a) {
    m_baseColorFallback = glm::vec4(r, g, b, a);
    return *this;
}

TexturedMaterial& TexturedMaterial::baseColorFactor(const glm::vec4& color) {
    m_baseColorFallback = color;
    return *this;
}

TexturedMaterial& TexturedMaterial::metallicFactor(float m) {
    m_metallicFallback = m;
    return *this;
}

TexturedMaterial& TexturedMaterial::roughnessFactor(float r) {
    m_roughnessFallback = r;
    return *this;
}

TexturedMaterial& TexturedMaterial::normalScale(float scale) {
    m_normalScale = scale;
    return *this;
}

TexturedMaterial& TexturedMaterial::aoStrength(float strength) {
    m_aoStrength = strength;
    return *this;
}

TexturedMaterial& TexturedMaterial::emissiveFactor(float r, float g, float b) {
    m_emissiveFallback = glm::vec3(r, g, b);
    return *this;
}

TexturedMaterial& TexturedMaterial::emissiveFactor(const glm::vec3& color) {
    m_emissiveFallback = color;
    return *this;
}

TexturedMaterial& TexturedMaterial::emissiveStrength(float strength) {
    m_emissiveStrength = strength;
    return *this;
}

// -------------------------------------------------------------------------
// Alpha and Culling
// -------------------------------------------------------------------------

TexturedMaterial& TexturedMaterial::alphaMode(AlphaMode mode) {
    m_alphaMode = mode;
    return *this;
}

TexturedMaterial& TexturedMaterial::alphaCutoff(float cutoff) {
    m_alphaCutoff = cutoff;
    return *this;
}

TexturedMaterial& TexturedMaterial::doubleSided(bool enabled) {
    m_doubleSided = enabled;
    return *this;
}

// -------------------------------------------------------------------------
// Texture Loading
// -------------------------------------------------------------------------

void TexturedMaterial::loadTexture(Context& ctx, TextureSlot& slot, bool srgb) {
    // Route to data-based loading if we have embedded data
    if (slot.hasData) {
        loadTextureFromData(ctx, slot, srgb);
        return;
    }

    if (slot.path.empty()) return;

    // Load image via vivid-io
    auto imageData = vivid::io::loadImage(slot.path);

    if (!imageData.valid()) {
        std::cerr << "TexturedMaterial: Failed to load: " << slot.path << std::endl;
        slot.needsLoad = false;  // Don't retry
        return;
    }

    int width = imageData.width;
    int height = imageData.height;

    // Create GPU texture
    WGPUTextureDescriptor texDesc = {};
    texDesc.label = toStringView("Material Texture");
    texDesc.size.width = static_cast<uint32_t>(width);
    texDesc.size.height = static_cast<uint32_t>(height);
    texDesc.size.depthOrArrayLayers = 1;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    // Use sRGB format for base color/emissive, linear for everything else
    texDesc.format = srgb ? WGPUTextureFormat_RGBA8UnormSrgb : WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;

    slot.texture = wgpuDeviceCreateTexture(ctx.device(), &texDesc);

    // Create texture view
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = texDesc.format;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;
    slot.view = wgpuTextureCreateView(slot.texture, &viewDesc);

    // Upload pixel data
    WGPUTexelCopyTextureInfo destination = {};
    destination.texture = slot.texture;
    destination.mipLevel = 0;
    destination.origin = {0, 0, 0};
    destination.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout dataLayout = {};
    dataLayout.offset = 0;
    dataLayout.bytesPerRow = static_cast<uint32_t>(width * 4);
    dataLayout.rowsPerImage = static_cast<uint32_t>(height);

    WGPUExtent3D writeSize = {
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        1
    };

    wgpuQueueWriteTexture(ctx.queue(), &destination, imageData.pixels.data(),
                          imageData.pixels.size(), &dataLayout, &writeSize);

    slot.needsLoad = false;

    std::cout << "TexturedMaterial: Loaded " << slot.path << " ("
              << width << "x" << height << ")" << std::endl;
}

void TexturedMaterial::loadTextureFromData(Context& ctx, TextureSlot& slot, bool srgb) {
    if (slot.pendingPixels.empty() || slot.pendingWidth <= 0 || slot.pendingHeight <= 0) {
        std::cerr << "TexturedMaterial: Invalid embedded texture data" << std::endl;
        slot.needsLoad = false;
        slot.hasData = false;
        return;
    }

    int width = slot.pendingWidth;
    int height = slot.pendingHeight;

    // Create GPU texture
    WGPUTextureDescriptor texDesc = {};
    texDesc.label = toStringView("Material Texture (embedded)");
    texDesc.size.width = static_cast<uint32_t>(width);
    texDesc.size.height = static_cast<uint32_t>(height);
    texDesc.size.depthOrArrayLayers = 1;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = srgb ? WGPUTextureFormat_RGBA8UnormSrgb : WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;

    slot.texture = wgpuDeviceCreateTexture(ctx.device(), &texDesc);

    // Create texture view
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = texDesc.format;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;
    slot.view = wgpuTextureCreateView(slot.texture, &viewDesc);

    // Upload pixel data
    WGPUTexelCopyTextureInfo destination = {};
    destination.texture = slot.texture;
    destination.mipLevel = 0;
    destination.origin = {0, 0, 0};
    destination.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout dataLayout = {};
    dataLayout.offset = 0;
    dataLayout.bytesPerRow = static_cast<uint32_t>(width * 4);
    dataLayout.rowsPerImage = static_cast<uint32_t>(height);

    WGPUExtent3D writeSize = {
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        1
    };

    wgpuQueueWriteTexture(ctx.queue(), &destination, slot.pendingPixels.data(),
                          slot.pendingPixels.size(), &dataLayout, &writeSize);

    slot.needsLoad = false;
    slot.hasData = false;
    slot.pendingPixels.clear();  // Free the CPU-side data
    slot.pendingWidth = 0;
    slot.pendingHeight = 0;

    std::cout << "TexturedMaterial: Loaded embedded texture ("
              << width << "x" << height << ")" << std::endl;
}

void TexturedMaterial::releaseTexture(TextureSlot& slot) {
    if (slot.view) {
        wgpuTextureViewRelease(slot.view);
        slot.view = nullptr;
    }
    if (slot.texture) {
        wgpuTextureRelease(slot.texture);
        slot.texture = nullptr;
    }
}

void TexturedMaterial::createDefaultTextures(Context& ctx) {
    // Create 1x1 default textures for missing maps
    WGPUTextureDescriptor texDesc = {};
    texDesc.label = toStringView("Default 1x1");
    texDesc.size.width = 1;
    texDesc.size.height = 1;
    texDesc.size.depthOrArrayLayers = 1;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyTextureInfo destination = {};
    destination.mipLevel = 0;
    destination.origin = {0, 0, 0};
    destination.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout dataLayout = {};
    dataLayout.offset = 0;
    dataLayout.bytesPerRow = 4;
    dataLayout.rowsPerImage = 1;

    WGPUExtent3D writeSize = {1, 1, 1};

    // Default white texture (for base color, AO - multiplicative defaults)
    uint8_t white[] = {255, 255, 255, 255};
    m_defaultWhite = wgpuDeviceCreateTexture(ctx.device(), &texDesc);
    m_defaultWhiteView = wgpuTextureCreateView(m_defaultWhite, &viewDesc);
    destination.texture = m_defaultWhite;
    wgpuQueueWriteTexture(ctx.queue(), &destination, white, 4, &dataLayout, &writeSize);

    // Default black texture (for emissive - additive default)
    uint8_t black[] = {0, 0, 0, 255};
    m_defaultBlack = wgpuDeviceCreateTexture(ctx.device(), &texDesc);
    m_defaultBlackView = wgpuTextureCreateView(m_defaultBlack, &viewDesc);
    destination.texture = m_defaultBlack;
    wgpuQueueWriteTexture(ctx.queue(), &destination, black, 4, &dataLayout, &writeSize);

    // Default normal texture (flat surface: tangent-space normal pointing up)
    // Normal map convention: (0.5, 0.5, 1.0) in [0,1] -> (0, 0, 1) in [-1,1]
    uint8_t flatNormal[] = {128, 128, 255, 255};
    m_defaultNormal = wgpuDeviceCreateTexture(ctx.device(), &texDesc);
    m_defaultNormalView = wgpuTextureCreateView(m_defaultNormal, &viewDesc);
    destination.texture = m_defaultNormal;
    wgpuQueueWriteTexture(ctx.queue(), &destination, flatNormal, 4, &dataLayout, &writeSize);
}

void TexturedMaterial::createSampler(Context& ctx) {
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.addressModeU = WGPUAddressMode_Repeat;
    samplerDesc.addressModeV = WGPUAddressMode_Repeat;
    samplerDesc.addressModeW = WGPUAddressMode_Repeat;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
    samplerDesc.lodMinClamp = 0.0f;
    samplerDesc.lodMaxClamp = 32.0f;
    samplerDesc.maxAnisotropy = 1;

    m_sampler = wgpuDeviceCreateSampler(ctx.device(), &samplerDesc);
}

// -------------------------------------------------------------------------
// Operator Interface
// -------------------------------------------------------------------------

void TexturedMaterial::init(Context& ctx) {
    if (m_initialized) return;

    // Create default textures first
    createDefaultTextures(ctx);
    createSampler(ctx);

    // Load any textures that have paths set
    if (m_baseColor.needsLoad) loadTexture(ctx, m_baseColor, true);   // sRGB
    if (m_normal.needsLoad) loadTexture(ctx, m_normal, false);        // Linear
    if (m_metallic.needsLoad) loadTexture(ctx, m_metallic, false);    // Linear
    if (m_roughness.needsLoad) loadTexture(ctx, m_roughness, false);  // Linear
    if (m_ao.needsLoad) loadTexture(ctx, m_ao, false);                // Linear
    if (m_emissive.needsLoad) loadTexture(ctx, m_emissive, true);     // sRGB

    // Set up cached views (use loaded textures or defaults)
    m_baseColorView = m_baseColor.view ? m_baseColor.view : m_defaultWhiteView;
    m_normalView = m_normal.view ? m_normal.view : m_defaultNormalView;
    m_metallicView = m_metallic.view ? m_metallic.view : m_defaultBlackView;  // Default 0
    m_roughnessView = m_roughness.view ? m_roughness.view : m_defaultWhiteView; // Default 1
    m_aoView = m_ao.view ? m_ao.view : m_defaultWhiteView;  // Default 1 (no occlusion)
    m_emissiveView = m_emissive.view ? m_emissive.view : m_defaultBlackView;  // Default no emission

    m_initialized = true;
}

void TexturedMaterial::process(Context& ctx) {
    // Check if any textures need reloading
    bool needsReload = m_baseColor.needsLoad || m_normal.needsLoad ||
                       m_metallic.needsLoad || m_roughness.needsLoad ||
                       m_ao.needsLoad || m_emissive.needsLoad;

    if (needsReload) {
        if (m_baseColor.needsLoad) {
            releaseTexture(m_baseColor);
            loadTexture(ctx, m_baseColor, true);
        }
        if (m_normal.needsLoad) {
            releaseTexture(m_normal);
            loadTexture(ctx, m_normal, false);
        }
        if (m_metallic.needsLoad) {
            releaseTexture(m_metallic);
            loadTexture(ctx, m_metallic, false);
        }
        if (m_roughness.needsLoad) {
            releaseTexture(m_roughness);
            loadTexture(ctx, m_roughness, false);
        }
        if (m_ao.needsLoad) {
            releaseTexture(m_ao);
            loadTexture(ctx, m_ao, false);
        }
        if (m_emissive.needsLoad) {
            releaseTexture(m_emissive);
            loadTexture(ctx, m_emissive, true);
        }

        // Update cached views
        m_baseColorView = m_baseColor.view ? m_baseColor.view : m_defaultWhiteView;
        m_normalView = m_normal.view ? m_normal.view : m_defaultNormalView;
        m_metallicView = m_metallic.view ? m_metallic.view : m_defaultBlackView;
        m_roughnessView = m_roughness.view ? m_roughness.view : m_defaultWhiteView;
        m_aoView = m_ao.view ? m_ao.view : m_defaultWhiteView;
        m_emissiveView = m_emissive.view ? m_emissive.view : m_defaultBlackView;
    }
}

void TexturedMaterial::cleanup() {
    releaseTexture(m_baseColor);
    releaseTexture(m_normal);
    releaseTexture(m_metallic);
    releaseTexture(m_roughness);
    releaseTexture(m_ao);
    releaseTexture(m_emissive);

    if (m_defaultWhiteView) wgpuTextureViewRelease(m_defaultWhiteView);
    if (m_defaultWhite) wgpuTextureRelease(m_defaultWhite);
    if (m_defaultBlackView) wgpuTextureViewRelease(m_defaultBlackView);
    if (m_defaultBlack) wgpuTextureRelease(m_defaultBlack);
    if (m_defaultNormalView) wgpuTextureViewRelease(m_defaultNormalView);
    if (m_defaultNormal) wgpuTextureRelease(m_defaultNormal);

    m_defaultWhiteView = nullptr;
    m_defaultWhite = nullptr;
    m_defaultBlackView = nullptr;
    m_defaultBlack = nullptr;
    m_defaultNormalView = nullptr;
    m_defaultNormal = nullptr;

    if (m_sampler) wgpuSamplerRelease(m_sampler);
    m_sampler = nullptr;

    m_baseColorView = nullptr;
    m_normalView = nullptr;
    m_metallicView = nullptr;
    m_roughnessView = nullptr;
    m_aoView = nullptr;
    m_emissiveView = nullptr;

    m_initialized = false;
}

} // namespace vivid::render3d
