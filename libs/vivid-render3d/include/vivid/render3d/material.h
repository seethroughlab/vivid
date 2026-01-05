#pragma once

/**
 * @file material.h
 * @brief PBR material system for Render3D
 *
 * Implements a physically-based rendering material using the metallic-roughness
 * workflow (as used in glTF 2.0). Materials can use either scalar values or
 * texture maps for each property.
 */

#include <glm/glm.hpp>
#include <webgpu/webgpu.h>
#include <memory>

namespace vivid {
class Context;
}

namespace vivid::render3d {

/**
 * @brief PBR material using metallic-roughness workflow
 *
 * Each property can be controlled via a scalar value, a texture, or both.
 * When a texture is present, the scalar value acts as a multiplier.
 *
 * @par Example
 * @code
 * PBRMaterial mat;
 * mat.baseColor(1.0f, 0.5f, 0.2f)     // Orange tint
 *    .metallic(0.0f)                   // Dielectric
 *    .roughness(0.4f)                  // Moderately smooth
 *    .normalScale(1.0f);               // Full normal strength
 * @endcode
 */
class PBRMaterial {
public:
    PBRMaterial();
    ~PBRMaterial();

    PBRMaterial(const PBRMaterial&) = delete;
    PBRMaterial& operator=(const PBRMaterial&) = delete;
    PBRMaterial(PBRMaterial&&) noexcept;
    PBRMaterial& operator=(PBRMaterial&&) noexcept;

    // -------------------------------------------------------------------------
    /// @name Base Color (Albedo)
    /// @{

    /// Set base color (linear RGB, not sRGB)
    PBRMaterial& baseColor(float r, float g, float b, float a = 1.0f);
    PBRMaterial& baseColor(const glm::vec4& color);

    /// Set base color texture (sRGB, will be converted to linear in shader)
    PBRMaterial& baseColorTexture(WGPUTextureView view);

    const glm::vec4& baseColorFactor() const { return m_baseColor; }
    WGPUTextureView baseColorTextureView() const { return m_baseColorTex; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Metallic-Roughness
    /// @{

    /// Set metallic factor (0 = dielectric, 1 = metal)
    PBRMaterial& metallic(float m);

    /// Set roughness factor (0 = smooth/mirror, 1 = rough/diffuse)
    PBRMaterial& roughness(float r);

    /// Set metallic-roughness texture (G = roughness, B = metallic)
    /// This follows glTF convention: roughness in G channel, metallic in B
    PBRMaterial& metallicRoughnessTexture(WGPUTextureView view);

    float metallicFactor() const { return m_metallic; }
    float roughnessFactor() const { return m_roughness; }
    WGPUTextureView metallicRoughnessTextureView() const { return m_metallicRoughnessTex; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Normal Mapping
    /// @{

    /// Set normal map texture (tangent-space normals)
    PBRMaterial& normalTexture(WGPUTextureView view);

    /// Set normal map scale (strength of normal perturbation)
    PBRMaterial& normalScale(float scale);

    float normalScaleFactor() const { return m_normalScale; }
    WGPUTextureView normalTextureView() const { return m_normalTex; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Ambient Occlusion
    /// @{

    /// Set ambient occlusion texture (R channel)
    PBRMaterial& occlusionTexture(WGPUTextureView view);

    /// Set occlusion strength (0 = no effect, 1 = full occlusion)
    PBRMaterial& occlusionStrength(float strength);

    float occlusionStrengthFactor() const { return m_occlusionStrength; }
    WGPUTextureView occlusionTextureView() const { return m_occlusionTex; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Emissive
    /// @{

    /// Set emissive color (linear RGB)
    PBRMaterial& emissive(float r, float g, float b);
    PBRMaterial& emissive(const glm::vec3& color);

    /// Set emissive texture (sRGB, will be converted to linear in shader)
    PBRMaterial& emissiveTexture(WGPUTextureView view);

    /// Set emissive intensity multiplier
    PBRMaterial& emissiveStrength(float strength);

    const glm::vec3& emissiveFactor() const { return m_emissive; }
    float emissiveStrengthFactor() const { return m_emissiveStrength; }
    WGPUTextureView emissiveTextureView() const { return m_emissiveTex; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Alpha/Transparency
    /// @{

    enum class AlphaMode {
        Opaque,     // Ignore alpha, render fully opaque
        Mask,       // Binary alpha test against cutoff
        Blend       // Traditional alpha blending
    };

    /// Set alpha mode
    PBRMaterial& alphaMode(AlphaMode mode);

    /// Set alpha cutoff for Mask mode
    PBRMaterial& alphaCutoff(float cutoff);

    /// Enable/disable double-sided rendering
    PBRMaterial& doubleSided(bool enabled);

    AlphaMode alphaModeValue() const { return m_alphaMode; }
    float alphaCutoffValue() const { return m_alphaCutoff; }
    bool isDoubleSided() const { return m_doubleSided; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name GPU Resources
    /// @{

    /// Check if material has any textures that require binding
    bool hasTextures() const;

    /// Get the uniform data for this material (for GPU buffer)
    struct UniformData {
        float baseColor[4];         // vec4f: 16 bytes, offset 0
        float emissive[3];          // vec3f: 12 bytes, offset 16
        float metallic;             // f32: 4 bytes, offset 28
        float roughness;            // f32: 4 bytes, offset 32
        float normalScale;          // f32: 4 bytes, offset 36
        float occlusionStrength;    // f32: 4 bytes, offset 40
        float emissiveStrength;     // f32: 4 bytes, offset 44
        float alphaCutoff;          // f32: 4 bytes, offset 48
        uint32_t alphaMode;         // u32: 4 bytes, offset 52
        uint32_t hasBaseColorTex;   // u32: 4 bytes, offset 56
        uint32_t hasMetallicRoughnessTex; // u32: 4 bytes, offset 60
        uint32_t hasNormalTex;      // u32: 4 bytes, offset 64
        uint32_t hasOcclusionTex;   // u32: 4 bytes, offset 68
        uint32_t hasEmissiveTex;    // u32: 4 bytes, offset 72
        uint32_t _pad[1];           // padding to 16-byte alignment
    };

    UniformData getUniformData() const;

    /// @}

private:
    // Base color
    glm::vec4 m_baseColor{1.0f, 1.0f, 1.0f, 1.0f};
    WGPUTextureView m_baseColorTex = nullptr;

    // Metallic-roughness
    float m_metallic = 0.0f;
    float m_roughness = 0.5f;
    WGPUTextureView m_metallicRoughnessTex = nullptr;

    // Normal
    float m_normalScale = 1.0f;
    WGPUTextureView m_normalTex = nullptr;

    // Occlusion
    float m_occlusionStrength = 1.0f;
    WGPUTextureView m_occlusionTex = nullptr;

    // Emissive
    glm::vec3 m_emissive{0.0f, 0.0f, 0.0f};
    float m_emissiveStrength = 1.0f;
    WGPUTextureView m_emissiveTex = nullptr;

    // Alpha
    AlphaMode m_alphaMode = AlphaMode::Opaque;
    float m_alphaCutoff = 0.5f;
    bool m_doubleSided = false;
};

} // namespace vivid::render3d
