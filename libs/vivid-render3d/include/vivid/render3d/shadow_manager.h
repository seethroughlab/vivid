#pragma once

#include <webgpu/webgpu.h>
#include <glm/glm.hpp>
#include <vector>

namespace vivid {
class Context;
}

namespace vivid::render3d {

struct LightData;
class LightOperator;
class Scene;

/**
 * @brief Manages shadow mapping for 3D rendering
 *
 * Handles both directional/spot shadows (single depth texture) and
 * point light shadows (6 separate 2D textures as cubemap workaround).
 *
 * Used by Render3D to render shadow passes and provide shadow sampling
 * bind groups to the main rendering pass.
 */
class ShadowManager {
public:
    ShadowManager();
    ~ShadowManager();

    // Non-copyable
    ShadowManager(const ShadowManager&) = delete;
    ShadowManager& operator=(const ShadowManager&) = delete;

    // -------------------------------------------------------------------------
    /// @name Control
    /// @{

    /// Enable/disable shadow mapping
    void setShadows(bool enabled);

    /// Set shadow map resolution (power of 2: 512, 1024, 2048)
    void setShadowMapResolution(int size);

    /// Check if shadows are enabled
    bool hasShadows() const { return m_shadowsEnabled; }

    /// Get shadow map resolution
    int shadowMapResolution() const { return m_shadowMapResolution; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Resource Lifecycle
    /// @{

    /// Initialize base resources (dummy textures, samplers, bind group layout)
    /// Called during Render3D::createPipeline(), before any shadow rendering
    void initializeBaseResources(Context& ctx);

    /// Create directional/spot shadow resources
    void createShadowResources(Context& ctx);

    /// Create point light shadow resources (6 separate 2D textures)
    void createPointShadowResources(Context& ctx);

    /// Destroy all shadow resources
    void destroyShadowResources();

    /// @}
    // -------------------------------------------------------------------------
    /// @name Rendering
    /// @{

    /// Render directional/spot light shadow map
    /// @param ctx Vivid context
    /// @param encoder Command encoder
    /// @param scene Scene to render
    /// @param light Primary light data
    /// @return true if shadow pass was rendered
    bool renderShadowPass(Context& ctx, WGPUCommandEncoder encoder,
                          const Scene& scene, const LightData& light);

    /// Render point light shadow maps (6 faces)
    /// @param ctx Vivid context
    /// @param encoder Command encoder
    /// @param scene Scene to render
    /// @param lightPos Point light position
    /// @param range Point light range
    /// @return true if shadow pass was rendered
    bool renderPointShadowPass(Context& ctx, WGPUCommandEncoder encoder,
                               const Scene& scene, const glm::vec3& lightPos, float range);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Bind Groups
    /// @{

    /// Get shadow sample bind group for main rendering pass
    WGPUBindGroup getShadowSampleBindGroup() const { return m_shadowSampleBindGroup; }

    /// Get shadow sample bind group layout
    WGPUBindGroupLayout getShadowSampleBindGroupLayout() const { return m_shadowSampleBindGroupLayout; }

    /// Update shadow sample bind group (call when shadow textures change)
    void updateShadowBindGroup(WGPUDevice device, bool hasDirShadow, bool hasPointShadow);

    /// Check if bind group needs rebuild
    bool isShadowBindGroupDirty() const { return m_shadowBindGroupDirty; }

    /// Mark shadow bind group as needing rebuild (call when light type changes)
    void markShadowBindGroupDirty() { m_shadowBindGroupDirty = true; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Resource State
    /// @{

    /// Check if directional/spot shadow resources are created
    bool hasShadowResources() const { return m_shadowPassPipeline != nullptr; }

    /// Check if point shadow resources are created
    bool hasPointShadowResources() const { return m_pointShadowPipeline != nullptr; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Texture Views (for main pass bind group)
    /// @{

    /// Get shadow map texture view
    WGPUTextureView getShadowMapView() const { return m_shadowMapView; }

    /// Get shadow sampler
    WGPUSampler getShadowSampler() const { return m_shadowSampler; }

    /// Get point shadow atlas texture view
    WGPUTextureView getPointShadowAtlasView() const { return m_pointShadowAtlasView; }

    /// Get point shadow sampler
    WGPUSampler getPointShadowSampler() const { return m_pointShadowSampler; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Shadow Sample Uniforms (for main pass)
    /// @{

    /// Get shadow sample uniform buffer
    WGPUBuffer getShadowSampleUniformBuffer() const { return m_shadowSampleUniformBuffer; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Light Matrices (for main pass uniforms)
    /// @{

    /// Get directional/spot light view-projection matrix
    const glm::mat4& getLightViewProj() const { return m_lightViewProj; }

    /// Get cached point light position
    const glm::vec3& getPointLightPos() const { return m_pointLightPos; }

    /// Get cached point light range
    float getPointLightRange() const { return m_pointLightRange; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Dummy Resources (for disabled shadows)
    /// @{

    /// Get dummy shadow texture view (1x1 white texture)
    WGPUTextureView getDummyShadowView() const { return m_dummyShadowView; }

    /// Get dummy point shadow atlas view
    WGPUTextureView getDummyPointShadowAtlasView() const { return m_dummyPointShadowAtlasView; }

    /// @}

private:
    // Light matrix computation
    glm::mat4 computeDirectionalLightMatrix(const LightData& light, const Scene& scene);
    glm::mat4 computeSpotLightMatrix(const LightData& light);
    glm::mat4 computePointLightFaceMatrix(const glm::vec3& lightPos, int face,
                                          float nearPlane, float farPlane);

    // Settings
    bool m_shadowsEnabled = false;
    int m_shadowMapResolution = 1024;

    // Directional/Spot shadow resources
    WGPUTexture m_shadowMapTexture = nullptr;
    WGPUTextureView m_shadowMapView = nullptr;
    WGPURenderPipeline m_shadowPassPipeline = nullptr;
    WGPUSampler m_shadowSampler = nullptr;
    WGPUBuffer m_shadowPassUniformBuffer = nullptr;
    WGPUBindGroupLayout m_shadowBindGroupLayout = nullptr;
    WGPUBindGroupLayout m_shadowSampleBindGroupLayout = nullptr;
    WGPUBindGroup m_shadowSampleBindGroup = nullptr;
    WGPUBindGroup m_shadowPassBindGroup = nullptr;
    WGPUBuffer m_shadowSampleUniformBuffer = nullptr;  // For main pass shadow sampling
    glm::mat4 m_lightViewProj = glm::mat4(1.0f);

    // Point light shadow resources (single 3x2 atlas texture)
    // Layout: 3 columns x 2 rows, each cell is shadowMapResolution x shadowMapResolution
    // Face order: +X(0,0), -X(1,0), +Y(2,0), -Y(0,1), +Z(1,1), -Z(2,1)
    WGPUTexture m_pointShadowAtlas = nullptr;
    WGPUTextureView m_pointShadowAtlasView = nullptr;
    WGPUTexture m_pointShadowDepthTexture = nullptr;
    WGPUTextureView m_pointShadowDepthView = nullptr;
    WGPURenderPipeline m_pointShadowPipeline = nullptr;
    WGPUBuffer m_pointShadowUniformBuffer = nullptr;
    WGPUBindGroup m_pointShadowSampleBindGroup = nullptr;
    WGPUSampler m_pointShadowSampler = nullptr;
    WGPUBindGroupLayout m_pointShadowBindGroupLayout = nullptr;
    WGPUBindGroup m_pointShadowPassBindGroup = nullptr;
    bool m_shadowBindGroupDirty = true;
    glm::vec3 m_pointLightPos = glm::vec3(0.0f);
    float m_pointLightRange = 50.0f;

    // Dummy resources (for when shadows are disabled)
    WGPUTexture m_dummyShadowTexture = nullptr;
    WGPUTextureView m_dummyShadowView = nullptr;
    WGPUTexture m_dummyPointShadowAtlas = nullptr;
    WGPUTextureView m_dummyPointShadowAtlasView = nullptr;
};

} // namespace vivid::render3d
