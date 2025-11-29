#pragma once
#include "renderer.h"
#include "mesh.h"
#include <vivid/graphics3d.h>
#include <webgpu/webgpu.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

namespace vivid {

// Forward declare CameraUniform from pipeline3d.h
struct CameraUniform;
CameraUniform makeCameraUniform(const Camera3D& camera, float aspectRatio);

/**
 * @brief 3D instanced render pipeline.
 *
 * Renders many instances of a mesh in a single draw call using GPU instancing.
 * Per-instance data (transform + color) is passed via instance buffer.
 */
class Pipeline3DInstanced {
public:
    Pipeline3DInstanced() = default;
    ~Pipeline3DInstanced();

    // Non-copyable, movable
    Pipeline3DInstanced(const Pipeline3DInstanced&) = delete;
    Pipeline3DInstanced& operator=(const Pipeline3DInstanced&) = delete;
    Pipeline3DInstanced(Pipeline3DInstanced&& other) noexcept;
    Pipeline3DInstanced& operator=(Pipeline3DInstanced&& other) noexcept;

    /**
     * @brief Create the instanced pipeline.
     */
    bool create(Renderer& renderer);

    /**
     * @brief Release all GPU resources.
     */
    void destroy();

    bool valid() const { return pipeline_ != nullptr; }

    WGPURenderPipeline pipeline() const { return pipeline_; }
    WGPUBindGroupLayout cameraBindGroupLayout() const { return cameraBindGroupLayout_; }

    /**
     * @brief Get vertex layout for instanced meshes (vertex + instance buffers).
     */
    static std::pair<WGPUVertexBufferLayout, WGPUVertexBufferLayout> getVertexLayouts();

private:
    WGPURenderPipeline pipeline_ = nullptr;
    WGPUBindGroupLayout cameraBindGroupLayout_ = nullptr;
    WGPUPipelineLayout pipelineLayout_ = nullptr;
    WGPUShaderModule shaderModule_ = nullptr;
    WGPUDevice device_ = nullptr;
};

/**
 * @brief Helper class for instanced 3D rendering.
 */
class Renderer3DInstanced {
public:
    Renderer3DInstanced() = default;
    ~Renderer3DInstanced();

    /**
     * @brief Initialize the instanced renderer.
     */
    void init(Renderer& renderer);

    /**
     * @brief Draw a mesh with many instances.
     *
     * @param mesh The mesh to render.
     * @param instances Per-instance data (transform + color).
     * @param camera The camera for viewing.
     * @param output Target texture.
     * @param clearColor Background color.
     */
    void drawInstanced(const Mesh& mesh,
                       const std::vector<Instance3D>& instances,
                       const Camera3D& camera,
                       Texture& output,
                       const glm::vec4& clearColor = {0, 0, 0, 1});

private:
    Renderer* renderer_ = nullptr;
    Pipeline3DInstanced pipeline_;

    // Camera uniform buffer
    WGPUBuffer cameraBuffer_ = nullptr;
    WGPUBindGroup cameraBindGroup_ = nullptr;

    // Instance buffer (resized as needed)
    WGPUBuffer instanceBuffer_ = nullptr;
    size_t instanceBufferCapacity_ = 0;

    // Depth buffer
    WGPUTexture depthTexture_ = nullptr;
    WGPUTextureView depthView_ = nullptr;
    int depthWidth_ = 0;
    int depthHeight_ = 0;

    void ensureDepthBuffer(int width, int height);
    void destroyDepthBuffer();
    void ensureInstanceBuffer(size_t count);
};

// Built-in instanced shader
namespace shaders3d {
extern const char* INSTANCED_COLOR;
}

} // namespace vivid
