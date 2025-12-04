// PBR Material Implementation

#include "vivid/pbr_material.h"
#include "vivid/context.h"

#include "RenderDevice.h"
#include "DeviceContext.h"
#include "TextureUtilities.h"
#include "TextureLoader.h"
#include "GraphicsTypesX.hpp"
#include "RefCntAutoPtr.hpp"

#include <filesystem>
#include <iostream>
#include <vector>

namespace vivid {

using namespace Diligent;
namespace fs = std::filesystem;

PBRMaterial::~PBRMaterial() {
    cleanup();
}

void PBRMaterial::cleanup() {
    if (sampler_) { sampler_->Release(); sampler_ = nullptr; }

    if (albedoSRV_) albedoSRV_ = nullptr;
    if (albedoTex_) { albedoTex_->Release(); albedoTex_ = nullptr; }

    if (normalSRV_) normalSRV_ = nullptr;
    if (normalTex_) { normalTex_->Release(); normalTex_ = nullptr; }

    if (metallicSRV_) metallicSRV_ = nullptr;
    if (metallicTex_) { metallicTex_->Release(); metallicTex_ = nullptr; }

    if (roughnessSRV_) roughnessSRV_ = nullptr;
    if (roughnessTex_) { roughnessTex_->Release(); roughnessTex_ = nullptr; }

    if (aoSRV_) aoSRV_ = nullptr;
    if (aoTex_) { aoTex_->Release(); aoTex_ = nullptr; }

    if (emissiveSRV_) emissiveSRV_ = nullptr;
    if (emissiveTex_) { emissiveTex_->Release(); emissiveTex_ = nullptr; }

    if (defaultWhiteSRV_) defaultWhiteSRV_ = nullptr;
    if (defaultWhiteTex_) { defaultWhiteTex_->Release(); defaultWhiteTex_ = nullptr; }

    if (defaultNormalSRV_) defaultNormalSRV_ = nullptr;
    if (defaultNormalTex_) { defaultNormalTex_->Release(); defaultNormalTex_ = nullptr; }
}

void PBRMaterial::createSampler(Context& ctx) {
    if (sampler_) return;

    SamplerDesc samplerDesc;
    samplerDesc.MinFilter = FILTER_TYPE_LINEAR;
    samplerDesc.MagFilter = FILTER_TYPE_LINEAR;
    samplerDesc.MipFilter = FILTER_TYPE_LINEAR;
    samplerDesc.AddressU = TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressV = TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressW = TEXTURE_ADDRESS_WRAP;
    samplerDesc.MaxAnisotropy = 8;

    ctx.device()->CreateSampler(samplerDesc, &sampler_);
}

bool PBRMaterial::loadTexture(Context& ctx, const std::string& path,
                              ITexture*& tex, ITextureView*& srv, bool srgb) {
    if (!fs::exists(path)) {
        return false;
    }

    // Use texture loader to get image data
    TextureLoadInfo loadInfo;
    loadInfo.IsSRGB = srgb;
    loadInfo.GenerateMips = true;
    loadInfo.Name = path.c_str();

    RefCntAutoPtr<ITextureLoader> pLoader;
    CreateTextureLoaderFromFile(path.c_str(), IMAGE_FILE_FORMAT_UNKNOWN, loadInfo, &pLoader);
    if (!pLoader) {
        std::cerr << "Failed to create texture loader: " << path << std::endl;
        return false;
    }

    // Get original texture description
    const TextureDesc& srcDesc = pLoader->GetTextureDesc();

    // Create a texture array with 1 slice (DiligentFX PBR shaders expect Texture2DArray)
    TextureDesc arrayDesc;
    arrayDesc.Name = path.c_str();
    arrayDesc.Type = RESOURCE_DIM_TEX_2D_ARRAY;  // Array texture required by DiligentFX PBR
    arrayDesc.Width = srcDesc.Width;
    arrayDesc.Height = srcDesc.Height;
    arrayDesc.ArraySize = 1;  // Single slice
    arrayDesc.MipLevels = srcDesc.MipLevels;
    arrayDesc.Format = srcDesc.Format;
    arrayDesc.BindFlags = BIND_SHADER_RESOURCE;
    arrayDesc.Usage = USAGE_IMMUTABLE;

    // Get subresource data for all mip levels
    std::vector<TextureSubResData> subResData(srcDesc.MipLevels);
    for (Uint32 mip = 0; mip < srcDesc.MipLevels; ++mip) {
        subResData[mip] = pLoader->GetSubresourceData(mip, 0);
    }

    TextureData initData;
    initData.pSubResources = subResData.data();
    initData.NumSubresources = srcDesc.MipLevels;

    RefCntAutoPtr<ITexture> texture;
    ctx.device()->CreateTexture(arrayDesc, &initData, &texture);

    if (!texture) {
        std::cerr << "Failed to create texture array: " << path << std::endl;
        return false;
    }

    tex = texture.Detach();
    srv = tex->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

    return true;
}

bool PBRMaterial::loadFromDirectory(Context& ctx, const std::string& dirPath, const std::string& prefix) {
    createSampler(ctx);
    createDefaults(ctx);

    // Try to find texture files
    std::string basePath = dirPath;
    if (!basePath.empty() && basePath.back() != '/') {
        basePath += '/';
    }

    // Determine prefix from directory name if not provided
    std::string filePrefix = prefix;
    if (filePrefix.empty()) {
        fs::path dirPathObj(dirPath);
        filePrefix = dirPathObj.filename().string();
        // Remove "-bl" suffix if present (common naming convention)
        if (filePrefix.size() > 3 && filePrefix.substr(filePrefix.size() - 3) == "-bl") {
            filePrefix = filePrefix.substr(0, filePrefix.size() - 3);
        }
    }

    // Try various naming conventions
    std::vector<std::string> albedoNames = {
        basePath + filePrefix + "_albedo.png",
        basePath + filePrefix + "_Albedo.png",
        basePath + filePrefix + "_basecolor.png",
        basePath + filePrefix + "_BaseColor.png",
        basePath + filePrefix + "_Base_Color.png",
        basePath + filePrefix + "_diffuse.png"
    };

    std::vector<std::string> normalNames = {
        basePath + filePrefix + "_normal-ogl.png",
        basePath + filePrefix + "_normal.png",
        basePath + filePrefix + "_Normal.png",
        basePath + filePrefix + "_Normal-ogl.png"
    };

    std::vector<std::string> metallicNames = {
        basePath + filePrefix + "_metallic.png",
        basePath + filePrefix + "_Metallic.png",
        basePath + filePrefix + "_metalness.png"
    };

    std::vector<std::string> roughnessNames = {
        basePath + filePrefix + "_roughness.png",
        basePath + filePrefix + "_Roughness.png"
    };

    std::vector<std::string> aoNames = {
        basePath + filePrefix + "_ao.png",
        basePath + filePrefix + "_AO.png",
        basePath + filePrefix + "_Ambient_Occlusion.png",
        basePath + filePrefix + "_ambient_occlusion.png"
    };

    // Load each texture type
    for (const auto& name : albedoNames) {
        if (loadTexture(ctx, name, albedoTex_, albedoSRV_, true)) {
            std::cout << "Loaded albedo: " << name << std::endl;
            break;
        }
    }

    for (const auto& name : normalNames) {
        if (loadTexture(ctx, name, normalTex_, normalSRV_, false)) {
            std::cout << "Loaded normal: " << name << std::endl;
            break;
        }
    }

    for (const auto& name : metallicNames) {
        if (loadTexture(ctx, name, metallicTex_, metallicSRV_, false)) {
            std::cout << "Loaded metallic: " << name << std::endl;
            break;
        }
    }

    for (const auto& name : roughnessNames) {
        if (loadTexture(ctx, name, roughnessTex_, roughnessSRV_, false)) {
            std::cout << "Loaded roughness: " << name << std::endl;
            break;
        }
    }

    for (const auto& name : aoNames) {
        if (loadTexture(ctx, name, aoTex_, aoSRV_, false)) {
            std::cout << "Loaded AO: " << name << std::endl;
            break;
        }
    }

    // At minimum, we need albedo
    return hasAlbedo();
}

bool PBRMaterial::loadAlbedo(Context& ctx, const std::string& path) {
    createSampler(ctx);
    return loadTexture(ctx, path, albedoTex_, albedoSRV_, true);
}

bool PBRMaterial::loadNormal(Context& ctx, const std::string& path) {
    createSampler(ctx);
    return loadTexture(ctx, path, normalTex_, normalSRV_, false);
}

bool PBRMaterial::loadMetallic(Context& ctx, const std::string& path) {
    createSampler(ctx);
    return loadTexture(ctx, path, metallicTex_, metallicSRV_, false);
}

bool PBRMaterial::loadRoughness(Context& ctx, const std::string& path) {
    createSampler(ctx);
    return loadTexture(ctx, path, roughnessTex_, roughnessSRV_, false);
}

bool PBRMaterial::loadAO(Context& ctx, const std::string& path) {
    createSampler(ctx);
    return loadTexture(ctx, path, aoTex_, aoSRV_, false);
}

bool PBRMaterial::loadEmissive(Context& ctx, const std::string& path) {
    createSampler(ctx);
    return loadTexture(ctx, path, emissiveTex_, emissiveSRV_, true);
}

void PBRMaterial::createDefaults(Context& ctx) {
    auto* device = ctx.device();

    // Create default white texture (for albedo, metallic, roughness fallbacks)
    // Use RESOURCE_DIM_TEX_2D_ARRAY to match DiligentFX PBR shader expectations
    if (!defaultWhiteTex_) {
        TextureDesc texDesc;
        texDesc.Name = "Default White";
        texDesc.Type = RESOURCE_DIM_TEX_2D_ARRAY;  // Array for DiligentFX PBR
        texDesc.Width = 1;
        texDesc.Height = 1;
        texDesc.ArraySize = 1;
        texDesc.Format = TEX_FORMAT_RGBA8_UNORM;
        texDesc.BindFlags = BIND_SHADER_RESOURCE;
        texDesc.Usage = USAGE_IMMUTABLE;

        Uint32 whitePixel = 0xFFFFFFFF;
        TextureSubResData subRes;
        subRes.pData = &whitePixel;
        subRes.Stride = sizeof(Uint32);

        TextureData initData;
        initData.pSubResources = &subRes;
        initData.NumSubresources = 1;

        device->CreateTexture(texDesc, &initData, &defaultWhiteTex_);
        defaultWhiteSRV_ = defaultWhiteTex_->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    }

    // Create default normal texture (flat normal = (0.5, 0.5, 1.0))
    if (!defaultNormalTex_) {
        TextureDesc texDesc;
        texDesc.Name = "Default Normal";
        texDesc.Type = RESOURCE_DIM_TEX_2D_ARRAY;  // Array for DiligentFX PBR
        texDesc.Width = 1;
        texDesc.Height = 1;
        texDesc.ArraySize = 1;
        texDesc.Format = TEX_FORMAT_RGBA8_UNORM;
        texDesc.BindFlags = BIND_SHADER_RESOURCE;
        texDesc.Usage = USAGE_IMMUTABLE;

        // Flat normal: (0.5, 0.5, 1.0, 1.0) = (128, 128, 255, 255)
        Uint32 normalPixel = 0xFFFF8080;  // ABGR
        TextureSubResData subRes;
        subRes.pData = &normalPixel;
        subRes.Stride = sizeof(Uint32);

        TextureData initData;
        initData.pSubResources = &subRes;
        initData.NumSubresources = 1;

        device->CreateTexture(texDesc, &initData, &defaultNormalTex_);
        defaultNormalSRV_ = defaultNormalTex_->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
    }

    createSampler(ctx);
}

} // namespace vivid
