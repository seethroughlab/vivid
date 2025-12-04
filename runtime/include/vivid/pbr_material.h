#pragma once

#include <string>
#include <glm/glm.hpp>

namespace Diligent {
    struct ITexture;
    struct ITextureView;
    struct ISampler;
}

namespace vivid {

class Context;

/// PBR Material with texture maps
class PBRMaterial {
public:
    PBRMaterial() = default;
    ~PBRMaterial();

    /// Load a complete PBR material from a directory
    /// Expects files named: *_albedo.png, *_normal*.png, *_metallic.png, *_roughness.png, *_ao.png
    bool loadFromDirectory(Context& ctx, const std::string& dirPath, const std::string& prefix = "");

    /// Load individual textures
    bool loadAlbedo(Context& ctx, const std::string& path);
    bool loadNormal(Context& ctx, const std::string& path);
    bool loadMetallic(Context& ctx, const std::string& path);
    bool loadRoughness(Context& ctx, const std::string& path);
    bool loadAO(Context& ctx, const std::string& path);
    bool loadEmissive(Context& ctx, const std::string& path);

    /// Create default white/normal textures for missing maps
    void createDefaults(Context& ctx);

    /// Release all resources
    void cleanup();

    // Texture accessors (return defaults if texture not loaded)
    Diligent::ITextureView* albedoSRV() const { return albedoSRV_ ? albedoSRV_ : defaultWhiteSRV_; }
    Diligent::ITextureView* normalSRV() const { return normalSRV_ ? normalSRV_ : defaultNormalSRV_; }
    Diligent::ITextureView* metallicSRV() const { return metallicSRV_ ? metallicSRV_ : defaultWhiteSRV_; }
    Diligent::ITextureView* roughnessSRV() const { return roughnessSRV_ ? roughnessSRV_ : defaultWhiteSRV_; }
    Diligent::ITextureView* aoSRV() const { return aoSRV_ ? aoSRV_ : defaultWhiteSRV_; }
    Diligent::ITextureView* emissiveSRV() const { return emissiveSRV_; }
    Diligent::ISampler* sampler() const { return sampler_; }

    // Default texture accessors
    Diligent::ITextureView* defaultWhiteSRV() const { return defaultWhiteSRV_; }
    Diligent::ITextureView* defaultNormalSRV() const { return defaultNormalSRV_; }

    /// Check if material has textures loaded
    bool hasAlbedo() const { return albedoTex_ != nullptr; }
    bool hasNormal() const { return normalTex_ != nullptr; }
    bool hasMetallic() const { return metallicTex_ != nullptr; }
    bool hasRoughness() const { return roughnessTex_ != nullptr; }
    bool hasAO() const { return aoTex_ != nullptr; }
    bool hasEmissive() const { return emissiveTex_ != nullptr; }

    /// Material properties (used when textures are not available)
    glm::vec4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;

private:
    bool loadTexture(Context& ctx, const std::string& path,
                    Diligent::ITexture*& tex, Diligent::ITextureView*& srv,
                    bool srgb = false);
    void createSampler(Context& ctx);

    Diligent::ITexture* albedoTex_ = nullptr;
    Diligent::ITextureView* albedoSRV_ = nullptr;

    Diligent::ITexture* normalTex_ = nullptr;
    Diligent::ITextureView* normalSRV_ = nullptr;

    Diligent::ITexture* metallicTex_ = nullptr;
    Diligent::ITextureView* metallicSRV_ = nullptr;

    Diligent::ITexture* roughnessTex_ = nullptr;
    Diligent::ITextureView* roughnessSRV_ = nullptr;

    Diligent::ITexture* aoTex_ = nullptr;
    Diligent::ITextureView* aoSRV_ = nullptr;

    Diligent::ITexture* emissiveTex_ = nullptr;
    Diligent::ITextureView* emissiveSRV_ = nullptr;

    // Default textures for missing maps
    Diligent::ITexture* defaultWhiteTex_ = nullptr;
    Diligent::ITextureView* defaultWhiteSRV_ = nullptr;
    Diligent::ITexture* defaultNormalTex_ = nullptr;
    Diligent::ITextureView* defaultNormalSRV_ = nullptr;

    Diligent::ISampler* sampler_ = nullptr;
};

} // namespace vivid
