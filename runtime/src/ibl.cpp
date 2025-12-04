// IBL (Image-Based Lighting) Implementation using DiligentFX

#include "vivid/ibl.h"
#include "vivid/context.h"

#include "RenderDevice.h"
#include "DeviceContext.h"
#include "TextureUtilities.h"
#include "PBR_Renderer.hpp"

#include <iostream>

namespace vivid {

using namespace Diligent;

IBLEnvironment::IBLEnvironment() = default;

IBLEnvironment::~IBLEnvironment() {
    cleanup();
}

bool IBLEnvironment::init(Context& ctx) {
    if (initialized_) return true;

    // Create PBR_Renderer for IBL processing
    PBR_Renderer::CreateInfo ci;
    ci.EnableIBL = true;
    ci.EnableAO = false;
    ci.EnableEmissive = false;
    ci.EnableClearCoat = false;
    ci.EnableSheen = false;
    ci.EnableAnisotropy = false;
    ci.EnableIridescence = false;
    ci.EnableTransmission = false;
    ci.EnableVolume = false;
    ci.EnableShadows = false;
    ci.CreateDefaultTextures = true;

    pbrRenderer_ = std::make_unique<PBR_Renderer>(
        ctx.device(),
        nullptr,  // No state cache
        ctx.immediateContext(),
        ci,
        false  // Don't init signature (we just need IBL processing)
    );

    initialized_ = true;
    std::cout << "IBL system initialized using DiligentFX PBR_Renderer" << std::endl;

    return true;
}

bool IBLEnvironment::loadHDR(Context& ctx, const std::string& hdrPath) {
    if (!init(ctx)) return false;

    // Load HDR texture
    TextureLoadInfo loadInfo;
    loadInfo.IsSRGB = false;  // HDR is linear
    loadInfo.GenerateMips = true;
    loadInfo.Name = hdrPath.c_str();

    RefCntAutoPtr<ITexture> texture;
    CreateTextureFromFile(hdrPath.c_str(), loadInfo, ctx.device(), &texture);

    if (!texture) {
        std::cerr << "Failed to load HDR: " << hdrPath << std::endl;
        return false;
    }

    // Release old texture if any
    if (envMapTex_) {
        envMapTex_->Release();
    }

    envMapTex_ = texture.Detach();
    envMapSRV_ = envMapTex_->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

    std::cout << "Loaded HDR environment: " << hdrPath << std::endl;

    // Precompute IBL cubemaps using PBR_Renderer
    pbrRenderer_->PrecomputeCubemaps(ctx.immediateContext(), envMapSRV_);

    std::cout << "Generated IBL cubemaps (irradiance + prefiltered)" << std::endl;

    return true;
}

bool IBLEnvironment::loadImage(Context& ctx, const std::string& imagePath) {
    if (!init(ctx)) return false;

    TextureLoadInfo loadInfo;
    loadInfo.IsSRGB = true;
    loadInfo.GenerateMips = true;
    loadInfo.Name = imagePath.c_str();

    RefCntAutoPtr<ITexture> texture;
    CreateTextureFromFile(imagePath.c_str(), loadInfo, ctx.device(), &texture);

    if (!texture) {
        std::cerr << "Failed to load environment image: " << imagePath << std::endl;
        return false;
    }

    if (envMapTex_) {
        envMapTex_->Release();
    }

    envMapTex_ = texture.Detach();
    envMapSRV_ = envMapTex_->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);

    std::cout << "Loaded environment image: " << imagePath << std::endl;

    // Precompute IBL cubemaps
    pbrRenderer_->PrecomputeCubemaps(ctx.immediateContext(), envMapSRV_);

    std::cout << "Generated IBL cubemaps" << std::endl;

    return true;
}

void IBLEnvironment::cleanup() {
    if (envMapSRV_) envMapSRV_ = nullptr;
    if (envMapTex_) {
        envMapTex_->Release();
        envMapTex_ = nullptr;
    }
    pbrRenderer_.reset();
    initialized_ = false;
}

ITextureView* IBLEnvironment::irradianceSRV() const {
    return pbrRenderer_ ? pbrRenderer_->GetIrradianceCubeSRV() : nullptr;
}

ITextureView* IBLEnvironment::prefilteredSRV() const {
    return pbrRenderer_ ? pbrRenderer_->GetPrefilteredEnvMapSRV() : nullptr;
}

ITextureView* IBLEnvironment::brdfLutSRV() const {
    return pbrRenderer_ ? pbrRenderer_->GetPreintegratedGGX_SRV() : nullptr;
}

} // namespace vivid
