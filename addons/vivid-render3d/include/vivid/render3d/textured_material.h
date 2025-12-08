#pragma once

/**
 * @file textured_material.h
 * @brief PBR material with texture map support
 *
 * Loads and manages PBR texture maps (albedo, normal, metallic, roughness, AO, emissive).
 * Texture paths are specified individually by the user.
 *
 * @par Example
 * @code
 * auto& material = chain.add<TexturedMaterial>("copper")
 *     .baseColor("assets/materials/metal/albedo.png")
 *     .normal("assets/materials/metal/normal.png")
 *     .metallic("assets/materials/metal/metallic.png")
 *     .roughness("assets/materials/metal/roughness.png")
 *     .ao("assets/materials/metal/ao.png");
 *
 * auto& render = chain.add<Render3D>("render")
 *     .input(&scene)
 *     .material(&material)
 *     .shadingMode(ShadingMode::PBR);
 * @endcode
 */

#include <vivid/operator.h>
#include <glm/glm.hpp>
#include <webgpu/webgpu.h>
#include <string>

namespace vivid {
class Context;
}

namespace vivid::render3d {

/**
 * @brief PBR material operator with texture map support
 *
 * Each texture map is optional. When not provided, a scalar fallback value is used.
 * Textures are loaded once during init() and cached.
 */
class TexturedMaterial : public Operator {
public:
    TexturedMaterial();
    ~TexturedMaterial() override;

    TexturedMaterial(const TexturedMaterial&) = delete;
    TexturedMaterial& operator=(const TexturedMaterial&) = delete;

    // -------------------------------------------------------------------------
    /// @name Texture Maps
    /// @{

    /// Set base color (albedo) texture path
    TexturedMaterial& baseColor(const std::string& path);

    /// Set normal map texture path (tangent-space, OpenGL convention)
    TexturedMaterial& normal(const std::string& path);

    /// Set metallic texture path (grayscale, white = metal)
    TexturedMaterial& metallic(const std::string& path);

    /// Set roughness texture path (grayscale, white = rough)
    TexturedMaterial& roughness(const std::string& path);

    /// Set ambient occlusion texture path (grayscale)
    TexturedMaterial& ao(const std::string& path);

    /// Set emissive texture path
    TexturedMaterial& emissive(const std::string& path);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Material Factors (multiplied with texture values)
    /// @{

    /// Set base color multiplier (linear RGB, default: white)
    TexturedMaterial& baseColorFactor(float r, float g, float b, float a = 1.0f);
    TexturedMaterial& baseColorFactor(const glm::vec4& color);

    /// Set metallic multiplier (0 = force dielectric, 1 = use texture, default: 1.0)
    TexturedMaterial& metallicFactor(float m);

    /// Set roughness multiplier (default: 1.0 to use texture values directly)
    TexturedMaterial& roughnessFactor(float r);

    /// Set normal map strength
    TexturedMaterial& normalScale(float scale);

    /// Set AO strength (0 = no effect, 1 = full)
    TexturedMaterial& aoStrength(float strength);

    /// Set emissive fallback (linear RGB)
    TexturedMaterial& emissiveFactor(float r, float g, float b);
    TexturedMaterial& emissiveFactor(const glm::vec3& color);

    /// Set emissive intensity multiplier
    TexturedMaterial& emissiveStrength(float strength);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Texture Access (for Render3D)
    /// @{

    WGPUTextureView baseColorView() const { return m_baseColorView; }
    WGPUTextureView normalView() const { return m_normalView; }
    WGPUTextureView metallicView() const { return m_metallicView; }
    WGPUTextureView roughnessView() const { return m_roughnessView; }
    WGPUTextureView aoView() const { return m_aoView; }
    WGPUTextureView emissiveView() const { return m_emissiveView; }

    bool hasBaseColorMap() const { return m_baseColorView != nullptr; }
    bool hasNormalMap() const { return m_normalView != nullptr; }
    bool hasMetallicMap() const { return m_metallicView != nullptr; }
    bool hasRoughnessMap() const { return m_roughnessView != nullptr; }
    bool hasAoMap() const { return m_aoView != nullptr; }
    bool hasEmissiveMap() const { return m_emissiveView != nullptr; }

    /// Get scalar fallback values
    const glm::vec4& getBaseColorFactor() const { return m_baseColorFallback; }
    float getMetallicFactor() const { return m_metallicFallback; }
    float getRoughnessFactor() const { return m_roughnessFallback; }
    float getNormalScale() const { return m_normalScale; }
    float getAoStrength() const { return m_aoStrength; }
    const glm::vec3& getEmissiveFactor() const { return m_emissiveFallback; }
    float getEmissiveStrength() const { return m_emissiveStrength; }

    /// Get the sampler for all material textures
    WGPUSampler sampler() const { return m_sampler; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "TexturedMaterial"; }

    /// @}

private:
    struct TextureSlot {
        std::string path;
        WGPUTexture texture = nullptr;
        WGPUTextureView view = nullptr;
        bool needsLoad = false;
    };

    void loadTexture(Context& ctx, TextureSlot& slot, bool srgb);
    void releaseTexture(TextureSlot& slot);
    void createDefaultTextures(Context& ctx);
    void createSampler(Context& ctx);

    // Texture paths and GPU resources
    TextureSlot m_baseColor;
    TextureSlot m_normal;
    TextureSlot m_metallic;
    TextureSlot m_roughness;
    TextureSlot m_ao;
    TextureSlot m_emissive;

    // Cached views (point to slot views or default textures)
    WGPUTextureView m_baseColorView = nullptr;
    WGPUTextureView m_normalView = nullptr;
    WGPUTextureView m_metallicView = nullptr;
    WGPUTextureView m_roughnessView = nullptr;
    WGPUTextureView m_aoView = nullptr;
    WGPUTextureView m_emissiveView = nullptr;

    // Default 1x1 textures for missing maps
    WGPUTexture m_defaultWhite = nullptr;
    WGPUTextureView m_defaultWhiteView = nullptr;
    WGPUTexture m_defaultBlack = nullptr;
    WGPUTextureView m_defaultBlackView = nullptr;
    WGPUTexture m_defaultNormal = nullptr;
    WGPUTextureView m_defaultNormalView = nullptr;

    // Shared sampler
    WGPUSampler m_sampler = nullptr;

    // Fallback values (factors multiply texture values, so default to 1.0 for full texture strength)
    glm::vec4 m_baseColorFallback{1.0f, 1.0f, 1.0f, 1.0f};
    float m_metallicFallback = 1.0f;
    float m_roughnessFallback = 1.0f;
    float m_normalScale = 1.0f;
    float m_aoStrength = 1.0f;
    glm::vec3 m_emissiveFallback{0.0f, 0.0f, 0.0f};
    float m_emissiveStrength = 1.0f;

    bool m_initialized = false;
};

} // namespace vivid::render3d
