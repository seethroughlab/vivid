#include <vivid/render3d/material.h>

namespace vivid::render3d {

PBRMaterial::PBRMaterial() = default;
PBRMaterial::~PBRMaterial() = default;

PBRMaterial::PBRMaterial(PBRMaterial&&) noexcept = default;
PBRMaterial& PBRMaterial::operator=(PBRMaterial&&) noexcept = default;

// -----------------------------------------------------------------------------
// Base Color
// -----------------------------------------------------------------------------

PBRMaterial& PBRMaterial::baseColor(float r, float g, float b, float a) {
    m_baseColor = glm::vec4(r, g, b, a);
    return *this;
}

PBRMaterial& PBRMaterial::baseColor(const glm::vec4& color) {
    m_baseColor = color;
    return *this;
}

PBRMaterial& PBRMaterial::baseColorTexture(WGPUTextureView view) {
    m_baseColorTex = view;
    return *this;
}

// -----------------------------------------------------------------------------
// Metallic-Roughness
// -----------------------------------------------------------------------------

PBRMaterial& PBRMaterial::metallic(float m) {
    m_metallic = glm::clamp(m, 0.0f, 1.0f);
    return *this;
}

PBRMaterial& PBRMaterial::roughness(float r) {
    m_roughness = glm::clamp(r, 0.0f, 1.0f);
    return *this;
}

PBRMaterial& PBRMaterial::metallicRoughnessTexture(WGPUTextureView view) {
    m_metallicRoughnessTex = view;
    return *this;
}

// -----------------------------------------------------------------------------
// Normal Mapping
// -----------------------------------------------------------------------------

PBRMaterial& PBRMaterial::normalTexture(WGPUTextureView view) {
    m_normalTex = view;
    return *this;
}

PBRMaterial& PBRMaterial::normalScale(float scale) {
    m_normalScale = scale;
    return *this;
}

// -----------------------------------------------------------------------------
// Ambient Occlusion
// -----------------------------------------------------------------------------

PBRMaterial& PBRMaterial::occlusionTexture(WGPUTextureView view) {
    m_occlusionTex = view;
    return *this;
}

PBRMaterial& PBRMaterial::occlusionStrength(float strength) {
    m_occlusionStrength = glm::clamp(strength, 0.0f, 1.0f);
    return *this;
}

// -----------------------------------------------------------------------------
// Emissive
// -----------------------------------------------------------------------------

PBRMaterial& PBRMaterial::emissive(float r, float g, float b) {
    m_emissive = glm::vec3(r, g, b);
    return *this;
}

PBRMaterial& PBRMaterial::emissive(const glm::vec3& color) {
    m_emissive = color;
    return *this;
}

PBRMaterial& PBRMaterial::emissiveTexture(WGPUTextureView view) {
    m_emissiveTex = view;
    return *this;
}

PBRMaterial& PBRMaterial::emissiveStrength(float strength) {
    m_emissiveStrength = strength;
    return *this;
}

// -----------------------------------------------------------------------------
// Alpha
// -----------------------------------------------------------------------------

PBRMaterial& PBRMaterial::alphaMode(AlphaMode mode) {
    m_alphaMode = mode;
    return *this;
}

PBRMaterial& PBRMaterial::alphaCutoff(float cutoff) {
    m_alphaCutoff = glm::clamp(cutoff, 0.0f, 1.0f);
    return *this;
}

PBRMaterial& PBRMaterial::doubleSided(bool enabled) {
    m_doubleSided = enabled;
    return *this;
}

// -----------------------------------------------------------------------------
// GPU Resources
// -----------------------------------------------------------------------------

bool PBRMaterial::hasTextures() const {
    return m_baseColorTex || m_metallicRoughnessTex ||
           m_normalTex || m_occlusionTex || m_emissiveTex;
}

PBRMaterial::UniformData PBRMaterial::getUniformData() const {
    UniformData data = {};

    data.baseColor[0] = m_baseColor.r;
    data.baseColor[1] = m_baseColor.g;
    data.baseColor[2] = m_baseColor.b;
    data.baseColor[3] = m_baseColor.a;

    data.emissive[0] = m_emissive.r;
    data.emissive[1] = m_emissive.g;
    data.emissive[2] = m_emissive.b;

    data.metallic = m_metallic;
    data.roughness = m_roughness;
    data.normalScale = m_normalScale;
    data.occlusionStrength = m_occlusionStrength;
    data.emissiveStrength = m_emissiveStrength;
    data.alphaCutoff = m_alphaCutoff;
    data.alphaMode = static_cast<uint32_t>(m_alphaMode);

    data.hasBaseColorTex = m_baseColorTex ? 1 : 0;
    data.hasMetallicRoughnessTex = m_metallicRoughnessTex ? 1 : 0;
    data.hasNormalTex = m_normalTex ? 1 : 0;
    data.hasOcclusionTex = m_occlusionTex ? 1 : 0;
    data.hasEmissiveTex = m_emissiveTex ? 1 : 0;

    return data;
}

} // namespace vivid::render3d
