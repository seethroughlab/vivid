#pragma once
#include "renderer.h"
#include "mesh.h"
#include "camera3d.h"
#include <webgpu/webgpu.h>
#include <glm/glm.hpp>
#include <string>

namespace vivid {

/**
 * @brief Transform uniform buffer for 3D objects.
 *
 * Per-object transform data passed to vertex shader.
 * Must match WGSL struct layout exactly.
 */
struct TransformUniform {
    glm::mat4 model;        // Model-to-world transform (64 bytes)
    glm::mat4 normalMatrix; // Inverse-transpose of model for normals (64 bytes)
};

/**
 * @brief 3D render pipeline for mesh rendering with depth testing.
 *
 * Creates and manages a WebGPU render pipeline configured for 3D geometry.
 * Supports vertex input (Vertex3D), depth testing, and uniform buffers.
 */
class Pipeline3D {
public:
    Pipeline3D() = default;
    ~Pipeline3D();

    // Non-copyable, movable
    Pipeline3D(const Pipeline3D&) = delete;
    Pipeline3D& operator=(const Pipeline3D&) = delete;
    Pipeline3D(Pipeline3D&& other) noexcept;
    Pipeline3D& operator=(Pipeline3D&& other) noexcept;

    /**
     * @brief Create a 3D pipeline from WGSL shader source.
     *
     * The shader should define:
     * - Vertex shader: vs_main with Vertex3D input
     * - Fragment shader: fs_main
     *
     * Bind groups:
     * - Group 0: Camera uniform (binding 0)
     * - Group 1: Transform uniform (binding 0)
     * - Group 2: Material data and textures (user-defined)
     *
     * @param renderer The renderer for GPU resources.
     * @param wgslSource Complete WGSL shader source.
     * @return true if creation succeeded.
     */
    bool create(Renderer& renderer, const std::string& wgslSource);

    /**
     * @brief Create a pipeline from a WGSL file.
     */
    bool createFromFile(Renderer& renderer, const std::string& path);

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

    /**
     * @brief Get the shader compilation error, if any.
     */
    const std::string& lastError() const { return lastError_; }

private:
    WGPURenderPipeline pipeline_ = nullptr;
    WGPUBindGroupLayout cameraBindGroupLayout_ = nullptr;
    WGPUBindGroupLayout transformBindGroupLayout_ = nullptr;
    WGPUPipelineLayout pipelineLayout_ = nullptr;
    WGPUShaderModule shaderModule_ = nullptr;

    WGPUDevice device_ = nullptr;
    std::string sourcePath_;
    std::string lastError_;
};

/**
 * @brief Helper class for rendering 3D scenes.
 *
 * Manages render pass setup with color and depth attachments,
 * and provides convenient methods for drawing meshes.
 */
class Renderer3D {
public:
    Renderer3D() = default;
    ~Renderer3D();

    /**
     * @brief Initialize the 3D renderer with a base renderer.
     * @param renderer The base renderer providing GPU context.
     */
    void init(Renderer& renderer);

    /**
     * @brief Begin a 3D render pass to a texture.
     *
     * Sets up color and depth attachments for 3D rendering.
     * Depth buffer is created/resized automatically.
     *
     * @param output Target color texture.
     * @param clearColor Background color.
     * @return Render pass encoder, or nullptr on failure.
     */
    WGPURenderPassEncoder beginRenderPass(Texture& output, const glm::vec4& clearColor = {0, 0, 0, 1});

    /**
     * @brief End the current render pass and submit commands.
     */
    void endRenderPass();

    /**
     * @brief Set the camera for subsequent draw calls.
     * @param camera The camera to use.
     * @param aspectRatio Width / height of render target.
     */
    void setCamera(const Camera3D& camera, float aspectRatio);

    /**
     * @brief Create a camera bind group for the current camera.
     * @param layout Bind group layout from Pipeline3D.
     * @return Bind group, or nullptr on failure.
     */
    WGPUBindGroup createCameraBindGroup(WGPUBindGroupLayout layout);

    /**
     * @brief Create a transform bind group for an object.
     * @param layout Bind group layout from Pipeline3D.
     * @param transform Model transform matrix.
     * @return Bind group, or nullptr on failure.
     */
    WGPUBindGroup createTransformBindGroup(WGPUBindGroupLayout layout, const glm::mat4& transform);

    /**
     * @brief Release a bind group created by this renderer.
     */
    void releaseBindGroup(WGPUBindGroup bindGroup);

private:
    Renderer* renderer_ = nullptr;
    WGPUCommandEncoder encoder_ = nullptr;
    WGPURenderPassEncoder renderPass_ = nullptr;

    // Camera uniform buffer (reused per frame)
    WGPUBuffer cameraBuffer_ = nullptr;
    CameraUniform currentCamera_;

    // Depth buffer management
    WGPUTexture depthTexture_ = nullptr;
    WGPUTextureView depthView_ = nullptr;
    int depthWidth_ = 0;
    int depthHeight_ = 0;

    void ensureDepthBuffer(int width, int height);
    void destroyDepthBuffer();
};

// Built-in 3D shaders
namespace shaders3d {

/**
 * @brief Simple unlit shader that displays vertex color based on normal.
 *
 * Useful for testing mesh rendering without lighting setup.
 */
extern const char* UNLIT_NORMAL;

/**
 * @brief Simple solid color shader.
 */
extern const char* SOLID_COLOR;

} // namespace shaders3d

} // namespace vivid
