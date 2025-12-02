#pragma once
#include "renderer.h"
#include <webgpu/webgpu.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <memory>
#include <functional>

namespace vivid {

// Forward declarations
struct Light;
struct SceneLighting;

/**
 * @brief Shadow map settings for quality control.
 */
struct ShadowSettings {
    int resolution = 2048;      ///< Shadow map resolution (512, 1024, 2048, 4096)
    float bias = 0.001f;        ///< Depth bias to prevent shadow acne
    float normalBias = 0.01f;   ///< Normal-based bias for steep surfaces
    float pcfRadius = 1.5f;     ///< PCF soft shadow radius
    bool pcfEnabled = true;     ///< Enable PCF soft shadows
    float maxDistance = 100.0f; ///< Maximum shadow distance from camera
    float strength = 1.0f;      ///< Shadow intensity (0 = no shadow, 1 = full)
};

/**
 * @brief GPU-compatible shadow uniform buffer.
 * Must match WGSL struct layout.
 */
struct ShadowUniform {
    glm::mat4 lightViewProj;    ///< Light's view-projection matrix
    float bias;                 ///< Depth bias
    float normalBias;           ///< Normal-based bias
    float pcfRadius;            ///< PCF sample radius
    float strength;             ///< Shadow strength (0-1)
    float texelSize;            ///< 1.0 / shadowMapResolution
    int pcfEnabled;             ///< PCF on/off (as int for WGSL)
    float _pad[2];              ///< Padding to 16-byte alignment
};

/**
 * @brief Shadow map for a single light source.
 * Handles depth texture creation and light matrix calculation.
 */
class ShadowMap {
public:
    ShadowMap() = default;
    ~ShadowMap();

    // Non-copyable
    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;

    // Move semantics
    ShadowMap(ShadowMap&& other) noexcept;
    ShadowMap& operator=(ShadowMap&& other) noexcept;

    /**
     * @brief Initialize the shadow map.
     * @param renderer The renderer for GPU resources.
     * @param resolution Shadow map resolution (width = height).
     * @return true if successful.
     */
    bool init(Renderer& renderer, int resolution);

    /**
     * @brief Destroy GPU resources.
     */
    void destroy();

    /**
     * @brief Check if shadow map is valid.
     */
    bool valid() const { return depthTexture_ != nullptr; }

    /**
     * @brief Get shadow map resolution.
     */
    int resolution() const { return resolution_; }

    /**
     * @brief Get the depth texture view for sampling.
     */
    WGPUTextureView depthView() const { return depthView_; }

    /**
     * @brief Get the depth texture for render attachment.
     */
    WGPUTexture depthTexture() const { return depthTexture_; }

    /**
     * @brief Calculate light view-projection matrix for a directional light.
     * @param lightDir Normalized light direction.
     * @param sceneCenter Center of the scene to shadow.
     * @param sceneRadius Radius encompassing the scene.
     * @return Light's view-projection matrix.
     */
    static glm::mat4 calcDirectionalLightMatrix(
        const glm::vec3& lightDir,
        const glm::vec3& sceneCenter,
        float sceneRadius
    );

    /**
     * @brief Calculate light view-projection matrix for a spot light.
     * @param position Light position.
     * @param direction Light direction.
     * @param outerAngle Outer cone angle in radians.
     * @param radius Light range.
     * @return Light's view-projection matrix.
     */
    static glm::mat4 calcSpotLightMatrix(
        const glm::vec3& position,
        const glm::vec3& direction,
        float outerAngle,
        float radius
    );

private:
    Renderer* renderer_ = nullptr;
    int resolution_ = 0;

    WGPUTexture depthTexture_ = nullptr;
    WGPUTextureView depthView_ = nullptr;
};

/**
 * @brief Depth-only render pipeline for shadow map generation.
 * Renders scene geometry to depth texture from light's perspective.
 */
class ShadowMapPipeline {
public:
    ShadowMapPipeline() = default;
    ~ShadowMapPipeline();

    // Non-copyable
    ShadowMapPipeline(const ShadowMapPipeline&) = delete;
    ShadowMapPipeline& operator=(const ShadowMapPipeline&) = delete;

    /**
     * @brief Initialize the shadow map pipeline.
     * @param renderer The renderer for GPU resources.
     * @return true if successful.
     */
    bool init(Renderer& renderer);

    /**
     * @brief Destroy GPU resources.
     */
    void destroy();

    /**
     * @brief Check if pipeline is valid.
     */
    bool valid() const { return pipeline_ != nullptr; }

    /**
     * @brief Begin shadow map render pass.
     * @param shadowMap The shadow map to render to.
     * @param lightViewProj The light's view-projection matrix.
     * @return Render pass encoder, or nullptr on failure.
     */
    WGPURenderPassEncoder beginShadowPass(ShadowMap& shadowMap, const glm::mat4& lightViewProj);

    /**
     * @brief End shadow map render pass.
     */
    void endShadowPass();

    /**
     * @brief Render a mesh to the shadow map.
     * @param vertexBuffer Mesh vertex buffer.
     * @param indexBuffer Mesh index buffer.
     * @param indexCount Number of indices.
     * @param modelMatrix Object's world transform.
     */
    void renderMesh(WGPUBuffer vertexBuffer, WGPUBuffer indexBuffer,
                    uint32_t indexCount, const glm::mat4& modelMatrix);

    /**
     * @brief Get bind group layout for transform uniform.
     */
    WGPUBindGroupLayout transformLayout() const { return transformLayout_; }

private:
    Renderer* renderer_ = nullptr;

    WGPURenderPipeline pipeline_ = nullptr;
    WGPUBindGroupLayout lightMatrixLayout_ = nullptr;
    WGPUBindGroupLayout transformLayout_ = nullptr;
    WGPUPipelineLayout pipelineLayout_ = nullptr;
    WGPUShaderModule shaderModule_ = nullptr;

    // Reusable buffers
    WGPUBuffer lightMatrixBuffer_ = nullptr;
    WGPUBuffer transformBuffer_ = nullptr;
    WGPUBindGroup lightMatrixBindGroup_ = nullptr;

    // Render pass state
    WGPUCommandEncoder encoder_ = nullptr;
    WGPURenderPassEncoder renderPass_ = nullptr;
    ShadowMap* currentShadowMap_ = nullptr;

    bool createPipeline();
};

/**
 * @brief Manages shadow maps for all shadow-casting lights in a scene.
 */
class ShadowManager {
public:
    ShadowManager() = default;
    ~ShadowManager() = default;

    /**
     * @brief Initialize the shadow manager.
     * @param renderer The renderer for GPU resources.
     * @param settings Default shadow settings.
     * @return true if successful.
     */
    bool init(Renderer& renderer, const ShadowSettings& settings = {});

    /**
     * @brief Destroy all resources.
     */
    void destroy();

    /**
     * @brief Update shadow settings.
     */
    void setSettings(const ShadowSettings& settings);

    /**
     * @brief Get current shadow settings.
     */
    const ShadowSettings& settings() const { return settings_; }

    /**
     * @brief Get the shadow map pipeline.
     */
    ShadowMapPipeline& pipeline() { return pipeline_; }

    /**
     * @brief Get or create shadow map for directional light.
     * @param lightIndex Index of the light in the scene.
     * @return Pointer to shadow map, or nullptr if shadows disabled.
     */
    ShadowMap* getDirectionalShadowMap(int lightIndex);

    /**
     * @brief Render shadow maps for all shadow-casting lights.
     * @param lighting Scene lighting with lights.
     * @param sceneCenter Center of the scene for shadow bounds.
     * @param sceneRadius Radius of the scene for shadow bounds.
     * @param renderCallback Callback to render scene geometry.
     */
    void renderShadowMaps(
        const SceneLighting& lighting,
        const glm::vec3& sceneCenter,
        float sceneRadius,
        std::function<void(ShadowMapPipeline&, const glm::mat4&)> renderCallback
    );

    /**
     * @brief Get shadow uniform data for shader.
     * @param lightIndex Index of the light.
     * @return Shadow uniform data.
     */
    ShadowUniform getShadowUniform(int lightIndex) const;

    /**
     * @brief Check if shadows are enabled.
     */
    bool enabled() const { return enabled_; }

    /**
     * @brief Enable or disable shadows.
     */
    void setEnabled(bool enabled) { enabled_ = enabled; }

private:
    Renderer* renderer_ = nullptr;
    ShadowSettings settings_;
    ShadowMapPipeline pipeline_;
    bool enabled_ = true;

    // Shadow maps for directional lights (index corresponds to light index)
    std::vector<std::unique_ptr<ShadowMap>> directionalShadowMaps_;
    std::vector<glm::mat4> lightMatrices_;  // Cached light matrices
};

} // namespace vivid
