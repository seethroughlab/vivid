#pragma once

#include <vivid/types.h>
#include <webgpu/webgpu.h>
#include <glm/glm.hpp>
#include <vector>

namespace vivid {

class Renderer;

/**
 * @brief Instance data for 2D circles
 */
struct CircleInstance {
    glm::vec2 position;  // Center position (0-1 normalized)
    float radius;        // Radius in normalized coords
    float _pad;          // Padding for alignment
    glm::vec4 color;     // RGBA color
};

/**
 * @brief 2D instanced rendering pipeline
 *
 * Renders 2D shapes (circles) using GPU instancing for efficiency.
 * All instances share the same circle mesh but have unique position/radius/color.
 */
class Pipeline2DInternal {
public:
    Pipeline2DInternal();
    ~Pipeline2DInternal();

    // Non-copyable, movable
    Pipeline2DInternal(const Pipeline2DInternal&) = delete;
    Pipeline2DInternal& operator=(const Pipeline2DInternal&) = delete;
    Pipeline2DInternal(Pipeline2DInternal&& other) noexcept;
    Pipeline2DInternal& operator=(Pipeline2DInternal&& other) noexcept;

    /**
     * @brief Initialize the 2D pipeline
     * @param renderer The renderer to use for GPU operations
     * @return true if initialization succeeded
     */
    bool init(Renderer& renderer);

    /**
     * @brief Clean up GPU resources
     */
    void destroy();

    /**
     * @brief Draw multiple circles with instancing
     * @param circles Vector of circle instances to draw
     * @param output Target texture to render into
     * @param clearColor Background color
     */
    void drawCircles(const std::vector<CircleInstance>& circles,
                     Texture& output, const glm::vec4& clearColor);

    bool isInitialized() const { return initialized_; }

private:
    void createCircleMesh();
    void createPipeline();

    Renderer* renderer_ = nullptr;
    bool initialized_ = false;

    // Circle mesh (shared by all instances)
    WGPUBuffer vertexBuffer_ = nullptr;
    WGPUBuffer indexBuffer_ = nullptr;
    uint32_t indexCount_ = 0;

    // Instance buffer (updated each frame)
    WGPUBuffer instanceBuffer_ = nullptr;
    size_t instanceBufferCapacity_ = 0;

    // Pipeline
    WGPUShaderModule shaderModule_ = nullptr;
    WGPURenderPipeline pipeline_ = nullptr;
    WGPUBindGroupLayout bindGroupLayout_ = nullptr;
    WGPUBuffer uniformBuffer_ = nullptr;
    WGPUSampler sampler_ = nullptr;
};

} // namespace vivid
