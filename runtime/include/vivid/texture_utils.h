#pragma once

#include <string>
#include <memory>
#include <vector>
#include <cstdint>

// Diligent Engine headers
#include "EngineFactoryVk.h"
#include "Texture.h"
#include "TextureView.h"
#include "Sampler.h"
#include "RefCntAutoPtr.hpp"

namespace vivid {

using namespace Diligent;

// Texture format presets for common use cases
enum class TexturePreset {
    RGBA8,          // Standard 8-bit RGBA (LDR)
    RGBA8_SRGB,     // sRGB color space (for display)
    RGBA16F,        // 16-bit float RGBA (HDR)
    RGBA32F,        // 32-bit float RGBA (high precision)
    R8,             // Single channel 8-bit (grayscale)
    R16F,           // Single channel 16-bit float
    R32F,           // Single channel 32-bit float
    RG8,            // Two channel 8-bit
    RG16F,          // Two channel 16-bit float
    Depth32F,       // Depth buffer
};

// Sampler filter modes
enum class FilterMode {
    Nearest,        // Point sampling (pixelated)
    Linear,         // Bilinear filtering (smooth)
    Trilinear,      // Trilinear with mipmaps
};

// Texture wrap modes
enum class WrapMode {
    Repeat,         // Tile the texture
    Mirror,         // Mirror at edges
    Clamp,          // Clamp to edge color
    Border,         // Use border color
};

// Texture creation description
struct TextureDesc {
    std::string name;
    uint32_t width = 256;
    uint32_t height = 256;
    TexturePreset format = TexturePreset::RGBA8;
    bool renderTarget = false;      // Can be used as render target
    bool generateMips = false;      // Generate mipmaps
    uint32_t mipLevels = 1;         // Number of mip levels (1 = no mips)
};

// Sampler creation description
struct SamplerDesc {
    FilterMode filter = FilterMode::Linear;
    WrapMode wrapU = WrapMode::Repeat;
    WrapMode wrapV = WrapMode::Repeat;
    float borderColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float maxAnisotropy = 1.0f;     // 1.0 = no anisotropy
};

// Managed texture with view and optional sampler
struct ManagedTexture {
    RefCntAutoPtr<ITexture> texture;
    RefCntAutoPtr<ITextureView> srv;     // Shader resource view (for sampling)
    RefCntAutoPtr<ITextureView> rtv;     // Render target view (if render target)
    RefCntAutoPtr<ISampler> sampler;
    uint32_t width = 0;
    uint32_t height = 0;
    TEXTURE_FORMAT format = TEX_FORMAT_UNKNOWN;

    bool isValid() const { return texture != nullptr; }
    operator bool() const { return isValid(); }
};

// Texture utilities for creating and loading textures
class TextureUtils {
public:
    TextureUtils(IRenderDevice* device);
    ~TextureUtils();

    // Non-copyable
    TextureUtils(const TextureUtils&) = delete;
    TextureUtils& operator=(const TextureUtils&) = delete;

    // Create an empty texture (for render targets or dynamic textures)
    ManagedTexture create(const TextureDesc& desc);

    // Create a texture from pixel data
    // Data should be in the format matching the preset (e.g., RGBA8 = 4 bytes per pixel)
    ManagedTexture createFromPixels(
        const std::string& name,
        uint32_t width,
        uint32_t height,
        TexturePreset format,
        const void* pixelData,
        size_t dataSize
    );

    // Load a texture from file (PNG, JPG, etc.)
    // Uses Diligent's built-in TextureLoader
    ManagedTexture loadFromFile(
        const std::string& filePath,
        bool generateMips = false,
        bool sRGB = true
    );

    // Create a sampler
    RefCntAutoPtr<ISampler> createSampler(const SamplerDesc& desc);

    // Create a default linear sampler
    RefCntAutoPtr<ISampler> createDefaultSampler();

    // Get the last error message
    const std::string& getLastError() const { return m_lastError; }

    // Utility: Convert preset to Diligent format
    static TEXTURE_FORMAT presetToFormat(TexturePreset preset);

    // Utility: Get bytes per pixel for a format
    static uint32_t getBytesPerPixel(TexturePreset preset);

private:
    IRenderDevice* m_device;
    std::string m_lastError;

    // Cached default sampler
    RefCntAutoPtr<ISampler> m_defaultSampler;

    // Helper to convert filter mode
    static FILTER_TYPE filterToType(FilterMode mode);

    // Helper to convert wrap mode
    static TEXTURE_ADDRESS_MODE wrapToMode(WrapMode mode);
};

} // namespace vivid
