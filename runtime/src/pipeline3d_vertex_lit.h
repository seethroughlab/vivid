#pragma once
#include "renderer.h"
#include "mesh.h"
#include <vivid/graphics3d.h>
#include <webgpu/webgpu.h>
#include <glm/glm.hpp>

namespace vivid {

/**
 * @brief GPU-compatible vertex-lit material uniform.
 * Must match the WGSL struct layout exactly.
 */
struct VertexLitMaterialUniform {
    glm::vec3 diffuse;
    float _pad1;
    glm::vec3 ambient;
    float ambientAmount;
    glm::vec3 emissive;
    float _pad2;
    int quantizeSteps;      // 0 = smooth, 2-5 = toon steps
    int hardSpecular;       // 0 or 1
    float specularPower;
    float specularThreshold;
    int hasTexture;         // 0 or 1
    float _pad3[3];
};

// Helper to convert VertexLitMaterial to GPU uniform
inline VertexLitMaterialUniform makeVertexLitMaterialUniform(const VertexLitMaterial& mat) {
    VertexLitMaterialUniform u = {};
    u.diffuse = mat.diffuse;
    u.ambient = mat.ambient;
    u.ambientAmount = mat.ambientAmount;
    u.emissive = mat.emissive;
    u.quantizeSteps = mat.quantizeSteps;
    u.hardSpecular = mat.hardSpecular ? 1 : 0;
    u.specularPower = mat.specularPower;
    u.specularThreshold = mat.specularThreshold;
    u.hasTexture = mat.diffuseMap != nullptr ? 1 : 0;
    return u;
}

/**
 * @brief Simple vertex-lit 3D rendering pipeline for retro aesthetics.
 *
 * Features:
 * - Simple N·L diffuse lighting
 * - Quantized shading (toon/PS1 style)
 * - Optional hard specular highlights
 * - Single diffuse texture support
 *
 * Bind groups:
 * - Group 0: Camera uniform
 * - Group 1: Transform uniform
 * - Group 2: Light direction uniform
 * - Group 3: Material uniform + optional texture
 */
class Pipeline3DVertexLit {
public:
    Pipeline3DVertexLit() = default;
    ~Pipeline3DVertexLit();

    // Non-copyable
    Pipeline3DVertexLit(const Pipeline3DVertexLit&) = delete;
    Pipeline3DVertexLit& operator=(const Pipeline3DVertexLit&) = delete;

    /**
     * @brief Initialize the vertex-lit pipeline.
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
     * @brief Render a mesh with vertex-lit shading.
     * @param mesh The mesh to render.
     * @param camera The camera viewpoint.
     * @param transform Model transform matrix.
     * @param material Vertex-lit material properties.
     * @param lightDir Directional light direction (normalized).
     * @param lightColor Light color (default white).
     * @param output Target texture to render into.
     * @param clearColor Background color.
     */
    void render(const Mesh3D& mesh, const Camera3D& camera, const glm::mat4& transform,
                const VertexLitMaterial& material,
                const glm::vec3& lightDir, const glm::vec3& lightColor,
                Texture& output, const glm::vec4& clearColor = {0, 0, 0, 1});

private:
    bool createPipeline(const std::string& shaderSource);

    Renderer* renderer_ = nullptr;

    WGPURenderPipeline pipeline_ = nullptr;
    WGPUBindGroupLayout cameraLayout_ = nullptr;
    WGPUBindGroupLayout transformLayout_ = nullptr;
    WGPUBindGroupLayout lightLayout_ = nullptr;
    WGPUBindGroupLayout materialLayout_ = nullptr;
    WGPUPipelineLayout pipelineLayout_ = nullptr;
    WGPUShaderModule shaderModule_ = nullptr;
    WGPUSampler textureSampler_ = nullptr;

    // Reusable buffers
    WGPUBuffer cameraBuffer_ = nullptr;
    WGPUBuffer transformBuffer_ = nullptr;
    WGPUBuffer lightBuffer_ = nullptr;
    WGPUBuffer materialBuffer_ = nullptr;

    // Default 1x1 white texture for when no texture is provided
    WGPUTexture defaultTexture_ = nullptr;
    WGPUTextureView defaultTextureView_ = nullptr;

    // Depth buffer
    WGPUTexture depthTexture_ = nullptr;
    WGPUTextureView depthView_ = nullptr;
    int depthWidth_ = 0;
    int depthHeight_ = 0;

    // Render pass state
    WGPUCommandEncoder encoder_ = nullptr;
    WGPURenderPassEncoder renderPass_ = nullptr;

    void ensureDepthBuffer(int width, int height);
    void destroyDepthBuffer();
};

// Built-in vertex-lit shader
namespace shaders3d {
extern const char* VERTEX_LIT;
} // namespace shaders3d

} // namespace vivid
