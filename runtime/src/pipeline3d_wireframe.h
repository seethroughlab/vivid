#pragma once
#include "renderer.h"
#include "mesh.h"
#include <vivid/graphics3d.h>
#include <webgpu/webgpu.h>
#include <glm/glm.hpp>

namespace vivid {

/**
 * @brief GPU-compatible wireframe material uniform.
 * Must match the WGSL struct layout exactly.
 */
struct WireframeMaterialUniform {
    glm::vec3 color;
    float opacity;
    float thickness;
    float _pad[3];
};

// Helper to convert WireframeMaterial to GPU uniform
inline WireframeMaterialUniform makeWireframeMaterialUniform(const WireframeMaterial& mat) {
    WireframeMaterialUniform u = {};
    u.color = mat.color;
    u.opacity = mat.opacity;
    u.thickness = mat.thickness;
    return u;
}

/**
 * @brief Wireframe 3D rendering pipeline using barycentric coordinates.
 *
 * Renders mesh edges by computing edge proximity in fragment shader.
 * Uses barycentric coordinates passed from vertex shader.
 *
 * Bind groups:
 * - Group 0: Camera uniform
 * - Group 1: Transform uniform
 * - Group 2: Material uniform
 */
class Pipeline3DWireframe {
public:
    Pipeline3DWireframe() = default;
    ~Pipeline3DWireframe();

    // Non-copyable
    Pipeline3DWireframe(const Pipeline3DWireframe&) = delete;
    Pipeline3DWireframe& operator=(const Pipeline3DWireframe&) = delete;

    /**
     * @brief Initialize the wireframe pipeline.
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
     * @brief Render a mesh as wireframe.
     * @param mesh The mesh to render.
     * @param camera The camera viewpoint.
     * @param transform Model transform matrix.
     * @param material Wireframe material properties.
     * @param output Target texture to render into.
     * @param clearColor Background color.
     */
    void render(const Mesh3D& mesh, const Camera3D& camera, const glm::mat4& transform,
                const WireframeMaterial& material,
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

    // Reusable buffers
    WGPUBuffer cameraBuffer_ = nullptr;
    WGPUBuffer transformBuffer_ = nullptr;
    WGPUBuffer materialBuffer_ = nullptr;

    // Depth buffer
    WGPUTexture depthTexture_ = nullptr;
    WGPUTextureView depthView_ = nullptr;
    int depthWidth_ = 0;
    int depthHeight_ = 0;

    void ensureDepthBuffer(int width, int height);
    void destroyDepthBuffer();
};

// Built-in wireframe shader
namespace shaders3d {
extern const char* WIREFRAME;
} // namespace shaders3d

} // namespace vivid
