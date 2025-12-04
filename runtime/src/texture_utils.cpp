// Vivid Texture Utilities Implementation

#include "vivid/texture_utils.h"

// Diligent Engine includes
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "Texture.h"
#include "TextureUtilities.h"
#include "RefCntAutoPtr.hpp"

#include <iostream>

namespace vivid {

using namespace Diligent;

TextureUtils::TextureUtils(IRenderDevice* device, IDeviceContext* context)
    : device_(device)
    , context_(context)
{
}

TextureUtils::~TextureUtils() {
    clearCache();
}

ManagedTexture TextureUtils::loadFromFile(const std::string& path, bool sRGB) {
    // Check cache first
    auto it = cache_.find(path);
    if (it != cache_.end()) {
        return it->second;
    }

    ManagedTexture result;

    TextureLoadInfo loadInfo;
    loadInfo.IsSRGB = sRGB;
    loadInfo.Name = path.c_str();

    RefCntAutoPtr<ITexture> texture;
    CreateTextureFromFile(path.c_str(), loadInfo, device_, &texture);

    if (!texture) {
        std::cerr << "Failed to load texture: " << path << std::endl;
        return result;
    }

    const auto& desc = texture->GetDesc();
    result.texture = texture.Detach();  // Transfer ownership
    result.width = desc.Width;
    result.height = desc.Height;

    // Get shader resource view
    result.srv = result.texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

    // Cache it
    cache_[path] = result;

    return result;
}

ManagedTexture TextureUtils::loadFromFileAsArray(const std::string& path, bool sRGB) {
    // Check cache first (with special key for array textures)
    std::string cacheKey = path + "_array";
    auto it = cache_.find(cacheKey);
    if (it != cache_.end()) {
        return it->second;
    }

    // First load as regular texture to get the data
    TextureLoadInfo loadInfo;
    loadInfo.IsSRGB = sRGB;
    loadInfo.Name = path.c_str();

    RefCntAutoPtr<ITexture> srcTexture;
    CreateTextureFromFile(path.c_str(), loadInfo, device_, &srcTexture);

    if (!srcTexture) {
        std::cerr << "Failed to load texture for array: " << path << std::endl;
        return ManagedTexture{};
    }

    const auto& srcDesc = srcTexture->GetDesc();

    // Create a Texture2DArray with 1 slice
    TextureDesc arrayDesc;
    arrayDesc.Name = (path + "_array").c_str();
    arrayDesc.Type = RESOURCE_DIM_TEX_2D_ARRAY;
    arrayDesc.Width = srcDesc.Width;
    arrayDesc.Height = srcDesc.Height;
    arrayDesc.ArraySize = 1;
    arrayDesc.MipLevels = srcDesc.MipLevels;
    arrayDesc.Format = srcDesc.Format;
    arrayDesc.BindFlags = BIND_SHADER_RESOURCE;
    arrayDesc.Usage = USAGE_DEFAULT;

    RefCntAutoPtr<ITexture> arrayTexture;
    device_->CreateTexture(arrayDesc, nullptr, &arrayTexture);

    if (!arrayTexture) {
        std::cerr << "Failed to create array texture: " << path << std::endl;
        return ManagedTexture{};
    }

    // Copy data from source to array texture
    CopyTextureAttribs copyAttribs;
    copyAttribs.pSrcTexture = srcTexture;
    copyAttribs.pDstTexture = arrayTexture;

    for (Uint32 mip = 0; mip < srcDesc.MipLevels; mip++) {
        copyAttribs.SrcMipLevel = mip;
        copyAttribs.DstMipLevel = mip;
        copyAttribs.SrcSlice = 0;
        copyAttribs.DstSlice = 0;
        context_->CopyTexture(copyAttribs);
    }

    ManagedTexture result;
    result.texture = arrayTexture.Detach();
    result.width = srcDesc.Width;
    result.height = srcDesc.Height;
    result.srv = result.texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

    cache_[cacheKey] = result;

    return result;
}

ManagedTexture TextureUtils::createRenderTarget(int width, int height, TEXTURE_FORMAT format) {
    // Default to RGBA8 SRGB if not specified
    if (format == static_cast<TEXTURE_FORMAT>(0)) {
        format = TEX_FORMAT_RGBA8_UNORM_SRGB;
    }

    TextureDesc desc;
    desc.Name = "RenderTarget";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.BindFlags = BIND_SHADER_RESOURCE | BIND_RENDER_TARGET;
    desc.Usage = USAGE_DEFAULT;

    RefCntAutoPtr<ITexture> texture;
    device_->CreateTexture(desc, nullptr, &texture);

    if (!texture) {
        std::cerr << "Failed to create render target" << std::endl;
        return ManagedTexture{};
    }

    ManagedTexture result;
    result.texture = texture.Detach();
    result.width = width;
    result.height = height;
    result.srv = result.texture->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    result.rtv = result.texture->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);

    return result;
}

ManagedTexture TextureUtils::createDepthStencil(int width, int height) {
    TextureDesc desc;
    desc.Name = "DepthStencil";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.Format = TEX_FORMAT_D32_FLOAT;
    desc.BindFlags = BIND_DEPTH_STENCIL;
    desc.Usage = USAGE_DEFAULT;

    RefCntAutoPtr<ITexture> texture;
    device_->CreateTexture(desc, nullptr, &texture);

    if (!texture) {
        std::cerr << "Failed to create depth stencil" << std::endl;
        return ManagedTexture{};
    }

    ManagedTexture result;
    result.texture = texture.Detach();
    result.width = width;
    result.height = height;
    // DSV is accessed via GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL) when needed

    return result;
}

void TextureUtils::release(ManagedTexture& texture) {
    if (texture.texture) {
        texture.texture->Release();
        texture.texture = nullptr;
        texture.srv = nullptr;
        texture.rtv = nullptr;
    }
}

void TextureUtils::clearCache() {
    for (auto& [key, tex] : cache_) {
        if (tex.texture) {
            tex.texture->Release();
        }
    }
    cache_.clear();
}

} // namespace vivid
