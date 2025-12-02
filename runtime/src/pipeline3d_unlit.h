#pragma once
#include "renderer.h"
#include "mesh.h"
#include <vivid/graphics3d.h>
#include <webgpu/webgpu.h>
#include <glm/glm.hpp>

namespace vivid {

/**
 * @brief GPU-compatible unlit material uniform.
 * Must match the WGSL struct layout exactly.
 */
struct UnlitMaterialUniform {
    glm::vec3 color;
    float opacity;
    int hasTexture;
    float _pad[3];
};

// Helper to convert UnlitMaterial to GPU uniform
inline UnlitMaterialUniform makeUnlitMaterialUniform(const UnlitMaterial& mat) {
    UnlitMaterialUniform u = {};
    u.color = mat.color;
    u.opacity = mat.opacity;
    u.hasTexture = mat.colorMap != nullptr ? 1 : 0;
    return u;
}

/**
 * @brief Simple unlit 3D rendering pipeline.
 *
 * No lighting calculations - just outputs color/texture directly.
 * Useful for UI elements, debug visualization, and stylized rendering.
 *
 * Bind groups:
 * - Group 0: Camera uniform
 * - Group 1: Transform uniform
 * - Group 2: Material uniform + optional texture
 */
class Pipeline3DUnlit {
public:
    Pipeline3DUnlit() = default;
    ~Pipeline3DUnlit();

    // Non-copyable
    Pipeline3DUnlit(const Pipeline3DUnlit&) = delete;
    Pipeline3DUnlit& operator=(const Pipeline3DUnlit&) = delete;

    /**
     * @brief Initialize the unlit pipeline.
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
     * @brief Render a mesh with unlit shading.
     * @param mesh The mesh to render.
     * @param camera The camera viewpoint.
     * @param transform Model transform matrix.
     * @param material Unlit material properties.
     * @param output Target texture to render into.
     * @param clearColor Background color.
     */
    void render(const Mesh3D& mesh, const Camera3D& camera, const glm::mat4& transform,
                const UnlitMaterial& material,
                Texture& output, const glm::vec4& clearColor = {0, 0, 0, 1});

private:
    bool createPipeline(const std::string& shaderSource);

    Renderer* renderer_ = nullptr;

    WGPURenderPipeline pipeline_ = nullptr;
    WGPUBindGroupLayout cameraLayout_ = nullptr;
    WGPUBindGroupLayout transformLayout_ = nullptr;
    WGPUBindGroupLayout materialLayout_ = nullptr;
    WGPUPipelineLayout pipelineLayout_ = nullptr;
    WGPUShaderModule shaderModule_ = nullptr;
    WGPUSampler textureSampler_ = nullptr;

    // Reusable buffers
    WGPUBuffer cameraBuffer_ = nullptr;
    WGPUBuffer transformBuffer_ = nullptr;
    WGPUBuffer materialBuffer_ = nullptr;

    // Default 1x1 white texture for when no texture is provided
    WGPUTexture defaultTexture_ = nullptr;
    WGPUTextureView defaultTextureView_ = nullptr;

    // Depth buffer
    WGPUTexture depthTexture_ = nullptr;
    WGPUTextureView depthView_ = nullptr;
    int depthWidth_ = 0;
    int depthHeight_ = 0;

    void ensureDepthBuffer(int width, int height);
    void destroyDepthBuffer();
};

// Built-in unlit shader
namespace shaders3d {
extern const char* UNLIT;
} // namespace shaders3d

} // namespace vivid
