#pragma once

// Vivid Effects 2D - Particle Renderer
// GPU-instanced rendering for circles and textured sprites

#include <vivid/effects/types.h>
#include <webgpu/webgpu.h>
#include <glm/glm.hpp>
#include <vector>

namespace vivid {
class Context;
}

namespace vivid::effects {

class ParticleRenderer {
public:
    ParticleRenderer() = default;
    ~ParticleRenderer();

    // Initialize GPU resources
    void init(WGPUDevice device, WGPUQueue queue);

    // Render circles using SDF (no texture needed)
    void renderCircles(Context& ctx,
                       const std::vector<Circle2D>& circles,
                       WGPUTextureView output,
                       int outputWidth,
                       int outputHeight,
                       const glm::vec4& clearColor);

    // Render textured sprites
    void renderSprites(Context& ctx,
                       const std::vector<Sprite2D>& sprites,
                       WGPUTextureView spriteTexture,
                       WGPUTextureView output,
                       int outputWidth,
                       int outputHeight,
                       const glm::vec4& clearColor);

    // Release all GPU resources
    void cleanup();

    bool isInitialized() const { return m_initialized; }

private:
    void createCirclePipeline();
    void createSpritePipeline();
    void createCircleMesh();  // 32-segment circle
    void createSpriteQuad();  // Simple quad for sprites
    void ensureInstanceCapacity(size_t count, bool forSprites);

    WGPUDevice m_device = nullptr;
    WGPUQueue m_queue = nullptr;
    bool m_initialized = false;

    // Circle rendering (SDF)
    WGPURenderPipeline m_circlePipeline = nullptr;
    WGPUBuffer m_circleVertexBuffer = nullptr;
    WGPUBuffer m_circleIndexBuffer = nullptr;
    WGPUBuffer m_circleInstanceBuffer = nullptr;
    WGPUBuffer m_circleUniformBuffer = nullptr;
    WGPUBindGroupLayout m_circleBindGroupLayout = nullptr;
    WGPUBindGroup m_circleBindGroup = nullptr;
    size_t m_circleInstanceCapacity = 0;
    uint32_t m_circleIndexCount = 0;

    // Sprite rendering (textured)
    WGPURenderPipeline m_spritePipeline = nullptr;
    WGPUBuffer m_spriteVertexBuffer = nullptr;
    WGPUBuffer m_spriteInstanceBuffer = nullptr;
    WGPUBuffer m_spriteUniformBuffer = nullptr;
    WGPUBindGroupLayout m_spriteBindGroupLayout = nullptr;
    WGPUSampler m_spriteSampler = nullptr;
    size_t m_spriteInstanceCapacity = 0;
};

} // namespace vivid::effects
