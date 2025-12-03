#include "vivid/texture_utils.h"

// Diligent Engine headers
#include "TextureLoader.h"
#include "Image.h"

#include <iostream>
#include <fstream>

namespace vivid {

using namespace Diligent;

TextureUtils::TextureUtils(IRenderDevice* device)
    : m_device(device) {
}

TextureUtils::~TextureUtils() = default;

TEXTURE_FORMAT TextureUtils::presetToFormat(TexturePreset preset) {
    switch (preset) {
        case TexturePreset::RGBA8:      return TEX_FORMAT_RGBA8_UNORM;
        case TexturePreset::RGBA8_SRGB: return TEX_FORMAT_RGBA8_UNORM_SRGB;
        case TexturePreset::RGBA16F:    return TEX_FORMAT_RGBA16_FLOAT;
        case TexturePreset::RGBA32F:    return TEX_FORMAT_RGBA32_FLOAT;
        case TexturePreset::R8:         return TEX_FORMAT_R8_UNORM;
        case TexturePreset::R16F:       return TEX_FORMAT_R16_FLOAT;
        case TexturePreset::R32F:       return TEX_FORMAT_R32_FLOAT;
        case TexturePreset::RG8:        return TEX_FORMAT_RG8_UNORM;
        case TexturePreset::RG16F:      return TEX_FORMAT_RG16_FLOAT;
        case TexturePreset::Depth32F:   return TEX_FORMAT_D32_FLOAT;
        default:                        return TEX_FORMAT_RGBA8_UNORM;
    }
}

uint32_t TextureUtils::getBytesPerPixel(TexturePreset preset) {
    switch (preset) {
        case TexturePreset::RGBA8:
        case TexturePreset::RGBA8_SRGB: return 4;
        case TexturePreset::RGBA16F:    return 8;
        case TexturePreset::RGBA32F:    return 16;
        case TexturePreset::R8:         return 1;
        case TexturePreset::R16F:       return 2;
        case TexturePreset::R32F:
        case TexturePreset::Depth32F:   return 4;
        case TexturePreset::RG8:        return 2;
        case TexturePreset::RG16F:      return 4;
        default:                        return 4;
    }
}

FILTER_TYPE TextureUtils::filterToType(FilterMode mode) {
    switch (mode) {
        case FilterMode::Nearest:   return FILTER_TYPE_POINT;
        case FilterMode::Linear:    return FILTER_TYPE_LINEAR;
        case FilterMode::Trilinear: return FILTER_TYPE_LINEAR;
        default:                    return FILTER_TYPE_LINEAR;
    }
}

TEXTURE_ADDRESS_MODE TextureUtils::wrapToMode(WrapMode mode) {
    switch (mode) {
        case WrapMode::Repeat: return TEXTURE_ADDRESS_WRAP;
        case WrapMode::Mirror: return TEXTURE_ADDRESS_MIRROR;
        case WrapMode::Clamp:  return TEXTURE_ADDRESS_CLAMP;
        case WrapMode::Border: return TEXTURE_ADDRESS_BORDER;
        default:               return TEXTURE_ADDRESS_WRAP;
    }
}

ManagedTexture TextureUtils::create(const TextureDesc& desc) {
    m_lastError.clear();
    ManagedTexture result;

    // Determine bind flags
    BIND_FLAGS bindFlags = BIND_SHADER_RESOURCE;
    if (desc.renderTarget) {
        if (desc.format == TexturePreset::Depth32F) {
            bindFlags |= BIND_DEPTH_STENCIL;
        } else {
            bindFlags |= BIND_RENDER_TARGET;
        }
    }

    // Create Diligent texture description
    Diligent::TextureDesc texDesc;
    texDesc.Name = desc.name.c_str();
    texDesc.Type = RESOURCE_DIM_TEX_2D;
    texDesc.Width = desc.width;
    texDesc.Height = desc.height;
    texDesc.Format = presetToFormat(desc.format);
    texDesc.MipLevels = desc.generateMips ? 0 : desc.mipLevels;
    texDesc.BindFlags = bindFlags;
    texDesc.Usage = USAGE_DEFAULT;

    if (desc.generateMips) {
        texDesc.MiscFlags = MISC_TEXTURE_FLAG_GENERATE_MIPS;
    }

    m_device->CreateTexture(texDesc, nullptr, &result.texture);

    if (!result.texture) {
        m_lastError = "Failed to create texture: " + desc.name;
        std::cerr << "[TextureUtils] " << m_lastError << std::endl;
        return result;
    }

    // Get default shader resource view
    result.srv = result.texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

    // Get render target view if applicable
    if (desc.renderTarget) {
        if (desc.format == TexturePreset::Depth32F) {
            result.rtv = result.texture->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
        } else {
            result.rtv = result.texture->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        }
    }

    result.width = desc.width;
    result.height = desc.height;
    result.format = presetToFormat(desc.format);

    std::cout << "[TextureUtils] Created texture: " << desc.name
              << " (" << desc.width << "x" << desc.height << ")" << std::endl;

    return result;
}

ManagedTexture TextureUtils::createFromPixels(
    const std::string& name,
    uint32_t width,
    uint32_t height,
    TexturePreset format,
    const void* pixelData,
    size_t dataSize
) {
    m_lastError.clear();
    ManagedTexture result;

    // Validate data size
    uint32_t expectedSize = width * height * getBytesPerPixel(format);
    if (dataSize < expectedSize) {
        m_lastError = "Insufficient pixel data for texture: " + name;
        std::cerr << "[TextureUtils] " << m_lastError << std::endl;
        return result;
    }

    // Create texture description
    Diligent::TextureDesc texDesc;
    texDesc.Name = name.c_str();
    texDesc.Type = RESOURCE_DIM_TEX_2D;
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.Format = presetToFormat(format);
    texDesc.MipLevels = 1;
    texDesc.BindFlags = BIND_SHADER_RESOURCE;
    texDesc.Usage = USAGE_IMMUTABLE;

    // Set up initial data
    TextureSubResData subResData;
    subResData.pData = pixelData;
    subResData.Stride = width * getBytesPerPixel(format);

    TextureData texData;
    texData.NumSubresources = 1;
    texData.pSubResources = &subResData;

    m_device->CreateTexture(texDesc, &texData, &result.texture);

    if (!result.texture) {
        m_lastError = "Failed to create texture from pixels: " + name;
        std::cerr << "[TextureUtils] " << m_lastError << std::endl;
        return result;
    }

    // Get default shader resource view
    result.srv = result.texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

    result.width = width;
    result.height = height;
    result.format = presetToFormat(format);

    std::cout << "[TextureUtils] Created texture from pixels: " << name
              << " (" << width << "x" << height << ")" << std::endl;

    return result;
}

ManagedTexture TextureUtils::loadFromFile(
    const std::string& filePath,
    bool generateMips,
    bool sRGB
) {
    m_lastError.clear();
    ManagedTexture result;

    // Check if file exists
    std::ifstream file(filePath);
    if (!file.is_open()) {
        m_lastError = "Failed to open texture file: " + filePath;
        std::cerr << "[TextureUtils] " << m_lastError << std::endl;
        return result;
    }
    file.close();

    // Configure texture loading
    TextureLoadInfo loadInfo;
    loadInfo.Name = filePath.c_str();
    loadInfo.IsSRGB = sRGB;
    loadInfo.GenerateMips = generateMips;

    // Use Diligent's texture loader
    RefCntAutoPtr<ITextureLoader> pLoader;
    CreateTextureLoaderFromFile(filePath.c_str(), IMAGE_FILE_FORMAT_UNKNOWN, loadInfo, &pLoader);

    if (!pLoader) {
        m_lastError = "Failed to create texture loader for: " + filePath;
        std::cerr << "[TextureUtils] " << m_lastError << std::endl;
        return result;
    }

    // Create texture from loader
    RefCntAutoPtr<ITexture> texture;
    pLoader->CreateTexture(m_device, &texture);

    if (!texture) {
        m_lastError = "Failed to load texture: " + filePath;
        std::cerr << "[TextureUtils] " << m_lastError << std::endl;
        return result;
    }

    result.texture = texture;
    result.srv = texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

    auto& desc = texture->GetDesc();
    result.width = desc.Width;
    result.height = desc.Height;
    result.format = desc.Format;

    std::cout << "[TextureUtils] Loaded texture: " << filePath
              << " (" << result.width << "x" << result.height << ")" << std::endl;

    return result;
}

RefCntAutoPtr<ISampler> TextureUtils::createSampler(const SamplerDesc& desc) {
    m_lastError.clear();

    Diligent::SamplerDesc samplerDesc;
    samplerDesc.MinFilter = filterToType(desc.filter);
    samplerDesc.MagFilter = filterToType(desc.filter);

    // Enable mipmapping for trilinear
    if (desc.filter == FilterMode::Trilinear) {
        samplerDesc.MipFilter = FILTER_TYPE_LINEAR;
    } else {
        samplerDesc.MipFilter = FILTER_TYPE_POINT;
    }

    samplerDesc.AddressU = wrapToMode(desc.wrapU);
    samplerDesc.AddressV = wrapToMode(desc.wrapV);
    samplerDesc.AddressW = TEXTURE_ADDRESS_WRAP;

    // Border color
    samplerDesc.BorderColor[0] = desc.borderColor[0];
    samplerDesc.BorderColor[1] = desc.borderColor[1];
    samplerDesc.BorderColor[2] = desc.borderColor[2];
    samplerDesc.BorderColor[3] = desc.borderColor[3];

    // Anisotropic filtering
    if (desc.maxAnisotropy > 1.0f) {
        samplerDesc.MinFilter = FILTER_TYPE_ANISOTROPIC;
        samplerDesc.MagFilter = FILTER_TYPE_ANISOTROPIC;
        samplerDesc.MaxAnisotropy = static_cast<Uint32>(desc.maxAnisotropy);
    }

    RefCntAutoPtr<ISampler> sampler;
    m_device->CreateSampler(samplerDesc, &sampler);

    if (!sampler) {
        m_lastError = "Failed to create sampler";
        std::cerr << "[TextureUtils] " << m_lastError << std::endl;
    }

    return sampler;
}

RefCntAutoPtr<ISampler> TextureUtils::createDefaultSampler() {
    if (!m_defaultSampler) {
        SamplerDesc desc;
        desc.filter = FilterMode::Linear;
        desc.wrapU = WrapMode::Repeat;
        desc.wrapV = WrapMode::Repeat;
        m_defaultSampler = createSampler(desc);
    }
    return m_defaultSampler;
}

} // namespace vivid
