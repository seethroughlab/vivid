#pragma once
#include "renderer.h"
#include "mesh.h"
#include <vivid/graphics3d.h>
#include <vivid/animation.h>
#include <webgpu/webgpu.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace vivid {

/**
 * @brief Bone matrices uniform buffer for skinned mesh shaders.
 * Supports up to MAX_BONES bones.
 */
struct BoneUniform {
    glm::mat4 bones[MAX_BONES];  // Array of bone transform matrices
};

/**
 * @brief Skinned mesh with GPU buffers for animated rendering.
 */
struct SkinnedMeshGPU {
    WGPUBuffer vertexBuffer = nullptr;
    WGPUBuffer indexBuffer = nullptr;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    bool valid() const { return vertexBuffer != nullptr && indexBuffer != nullptr; }
};

/**
 * @brief 3D render pipeline for skinned mesh rendering with skeletal animation.
 *
 * Creates and manages a WebGPU render pipeline configured for skinned geometry.
 * Supports SkinnedVertex3D input (with bone weights), depth testing, and bone uniform buffers.
 */
class Pipeline3DSkinnedInternal {
public:
    Pipeline3DSkinnedInternal() = default;
    ~Pipeline3DSkinnedInternal();

    // Non-copyable, movable
    Pipeline3DSkinnedInternal(const Pipeline3DSkinnedInternal&) = delete;
    Pipeline3DSkinnedInternal& operator=(const Pipeline3DSkinnedInternal&) = delete;
    Pipeline3DSkinnedInternal(Pipeline3DSkinnedInternal&& other) noexcept;
    Pipeline3DSkinnedInternal& operator=(Pipeline3DSkinnedInternal&& other) noexcept;

    /**
     * @brief Create a skinned 3D pipeline from WGSL shader source.
     *
     * Bind groups:
     * - Group 0: Camera uniform (binding 0)
     * - Group 1: Transform uniform (binding 0)
     * - Group 2: Bone matrices uniform (binding 0)
     *
     * @param renderer The renderer for GPU resources.
     * @param wgslSource Complete WGSL shader source.
     * @return true if creation succeeded.
     */
    bool create(Renderer& renderer, const std::string& wgslSource);

    /**
     * @brief Create using the built-in skinned shader.
     */
    bool create(Renderer& renderer);

    /**
     * @brief Release all GPU resources.
     */
    void destroy();

    /**
     * @brief Check if pipeline is valid.
     */
    bool valid() const { return pipeline_ != nullptr; }

    // Accessors for bind group creation
    WGPURenderPipeline pipeline() const { return pipeline_; }
    WGPUBindGroupLayout cameraBindGroupLayout() const { return cameraBindGroupLayout_; }
    WGPUBindGroupLayout transformBindGroupLayout() const { return transformBindGroupLayout_; }
    WGPUBindGroupLayout boneBindGroupLayout() const { return boneBindGroupLayout_; }

    /**
     * @brief Get the shader compilation error, if any.
     */
    const std::string& lastError() const { return lastError_; }

private:
    WGPURenderPipeline pipeline_ = nullptr;
    WGPUBindGroupLayout cameraBindGroupLayout_ = nullptr;
    WGPUBindGroupLayout transformBindGroupLayout_ = nullptr;
    WGPUBindGroupLayout boneBindGroupLayout_ = nullptr;
    WGPUPipelineLayout pipelineLayout_ = nullptr;
    WGPUShaderModule shaderModule_ = nullptr;

    WGPUDevice device_ = nullptr;
    std::string lastError_;
};

/**
 * @brief Helper class for rendering skinned meshes.
 */
class SkinnedMeshRenderer {
public:
    SkinnedMeshRenderer() = default;
    ~SkinnedMeshRenderer();

    /**
     * @brief Initialize with a base renderer.
     */
    void init(Renderer& renderer);

    /**
     * @brief Create GPU buffers for a skinned mesh.
     */
    SkinnedMeshGPU createMesh(const std::vector<SkinnedVertex3D>& vertices,
                               const std::vector<uint32_t>& indices);

    /**
     * @brief Destroy a skinned mesh.
     */
    void destroyMesh(SkinnedMeshGPU& mesh);

    /**
     * @brief Create a bone matrices bind group.
     * @param layout Bind group layout from Pipeline3DSkinnedInternal.
     * @param boneMatrices The bone transform matrices.
     * @return Bind group, or nullptr on failure.
     */
    WGPUBindGroup createBoneBindGroup(WGPUBindGroupLayout layout,
                                       const std::vector<glm::mat4>& boneMatrices);

    /**
     * @brief Update bone matrices in an existing bind group's buffer.
     */
    void updateBoneMatrices(WGPUBuffer boneBuffer,
                            const std::vector<glm::mat4>& boneMatrices);

    /**
     * @brief Release a bind group created by this renderer.
     */
    void releaseBindGroup(WGPUBindGroup bindGroup);

    /**
     * @brief Get the underlying renderer.
     */
    Renderer* renderer() const { return renderer_; }

private:
    Renderer* renderer_ = nullptr;
    std::vector<WGPUBuffer> boneBuffers_;
};

// Built-in skinned mesh shader
namespace shadersSkinned {

/**
 * @brief Skinned mesh shader with bone matrix transforms.
 * Displays normal-based coloring for testing.
 */
extern const char* SKINNED_UNLIT;

} // namespace shadersSkinned

} // namespace vivid
