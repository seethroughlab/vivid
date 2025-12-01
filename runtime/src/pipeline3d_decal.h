#pragma once
#include "renderer.h"
#include "mesh.h"
#include <vivid/graphics3d.h>
#include <webgpu/webgpu.h>
#include <glm/glm.hpp>
#include <string>

namespace vivid {

/**
 * @brief GPU-compatible decal uniform buffer.
 * Must match the WGSL struct layout exactly.
 */
struct DecalUniform {
    glm::mat4 decalMatrix;      // World-to-decal space transform (64 bytes)
    glm::mat4 invViewProj;      // Inverse view-projection for depth reconstruction (64 bytes)
    glm::vec4 color;            // Decal color and opacity (16 bytes)
    float depthBias;            // Z-bias for depth comparison (4 bytes)
    int32_t blendMode;          // Blend mode enum (4 bytes)
    int32_t wrapU;              // Wrap in U direction (4 bytes)
    int32_t wrapV;              // Wrap in V direction (4 bytes)
};

// Helper to convert Decal to GPU uniform
inline DecalUniform makeDecalUniform(const Decal& decal, const glm::mat4& invViewProj) {
    DecalUniform u = {};
    u.decalMatrix = decal.projectionMatrix();
    u.invViewProj = invViewProj;
    u.color = decal.color;
    u.depthBias = decal.depthBias;
    u.blendMode = static_cast<int32_t>(decal.blendMode);
    u.wrapU = decal.wrapU ? 1 : 0;
    u.wrapV = decal.wrapV ? 1 : 0;
    return u;
}

/**
 * @brief Decal projection pipeline.
 *
 * Renders decals projected onto 3D geometry using depth buffer reconstruction.
 * Decals are rendered as a post-process pass over the existing color buffer.
 *
 * Bind groups:
 * - Group 0: Decal uniform buffer
 * - Group 1: Depth texture + decal texture + sampler
 */
class Pipeline3DDecal {
public:
    Pipeline3DDecal() = default;
    ~Pipeline3DDecal();

    // Non-copyable
    Pipeline3DDecal(const Pipeline3DDecal&) = delete;
    Pipeline3DDecal& operator=(const Pipeline3DDecal&) = delete;

    /**
     * @brief Initialize the decal pipeline.
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
    bool valid() const { return pipelines_[0] != nullptr; }

    /**
     * @brief Render a decal onto an existing color buffer.
     *
     * @param decal The decal configuration.
     * @param camera The camera used to render the scene.
     * @param depthTexture The depth buffer from the scene render.
     * @param colorOutput The color buffer to blend the decal onto (read-write).
     */
    void renderDecal(const Decal& decal, const Camera3D& camera,
                     const Texture& depthTexture, Texture& colorOutput);

    /**
     * @brief Render multiple decals in a single pass.
     *
     * More efficient than calling renderDecal multiple times.
     */
    void renderDecals(const std::vector<Decal>& decals, const Camera3D& camera,
                      const Texture& depthTexture, Texture& colorOutput);

private:
    bool createPipelines();
    WGPURenderPipeline createBlendPipeline(DecalBlendMode mode);
    WGPURenderPipeline getPipelineForBlendMode(DecalBlendMode mode);

    Renderer* renderer_ = nullptr;

    // One pipeline per blend mode (Normal, Multiply, Additive, Overlay)
    static constexpr int BLEND_MODE_COUNT = 4;
    WGPURenderPipeline pipelines_[BLEND_MODE_COUNT] = {nullptr, nullptr, nullptr, nullptr};

    WGPUBindGroupLayout decalUniformLayout_ = nullptr;
    WGPUBindGroupLayout textureLayout_ = nullptr;
    WGPUPipelineLayout pipelineLayout_ = nullptr;
    WGPUShaderModule shaderModule_ = nullptr;
    WGPUSampler sampler_ = nullptr;
    WGPUSampler depthSampler_ = nullptr;  // Non-filtering sampler for depth

    // Reusable buffer
    WGPUBuffer decalBuffer_ = nullptr;

    // Full-screen quad mesh
    WGPUBuffer quadVertexBuffer_ = nullptr;
    WGPUBuffer quadIndexBuffer_ = nullptr;
};

// Built-in decal shader
namespace shaders3d {
extern const char* DECAL_PROJECTION;
} // namespace shaders3d

} // namespace vivid
