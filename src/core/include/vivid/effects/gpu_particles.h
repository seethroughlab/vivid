#pragma once

// Vivid Effects 2D - GPU Particles Operator
// Compute shader-based particle system with curl noise and force fields
// Scales to 100k+ particles using WebGPU compute

#include <vivid/effects/texture_operator.h>
#include <vivid/effects/gpu_handle.h>
#include <vivid/param.h>
#include <glm/glm.hpp>
#include <vector>
#include <random>

namespace vivid::effects {

/// GPU particle data structure (64 bytes, cache-aligned)
struct GPUParticle {
    float posX, posY;       // 8 bytes - position in normalized 0-1 space
    float velX, velY;       // 8 bytes - velocity
    float life;             // 4 bytes - remaining life
    float maxLife;          // 4 bytes - initial life (for age calculation)
    float size;             // 4 bytes - particle size
    float rotation;         // 4 bytes - for sprites
    float colorR, colorG, colorB, colorA;  // 16 bytes - particle color
    float seed;             // 4 bytes - per-particle random seed
    float _pad[3];          // 12 bytes - alignment to 64 bytes
};
static_assert(sizeof(GPUParticle) == 64, "GPUParticle must be 64 bytes");

/// Emitter shape for particle spawning
enum class GPUEmitterShape {
    Point,      // Single point emitter
    Line,       // Line segment emitter
    Ring,       // Circle outline emitter
    Disc,       // Filled circle emitter
    Rectangle   // Rectangle area emitter
};

/// Color mode for particle coloring
enum class GPUColorMode {
    Solid,      // Single color
    Gradient,   // Interpolate start to end color over lifetime
    Rainbow,    // HSV rainbow based on particle index
    Random      // Random color per particle
};

/**
 * @brief GPU compute-based particle system
 *
 * Uses WebGPU compute shaders for particle simulation, enabling 100k+ particles
 * with force field-based physics including curl noise, vortex, and gravity.
 *
 * @par Example
 * @code
 * auto& particles = chain.add<GPUParticles>("flow");
 * particles.maxParticles = 100000;
 * particles.emitRate = 2000.0f;
 * particles.curlStrength = 0.8f;
 * particles.curlScale = 6.0f;
 * particles.curlSpeed = 0.5f;
 * particles.colorStart.set(1.0f, 0.6f, 0.2f, 1.0f);
 * particles.colorEnd.set(1.0f, 0.0f, 0.0f, 0.0f);
 * @endcode
 */
class GPUParticles : public TextureOperator {
public:
    GPUParticles();
    ~GPUParticles() override;

    // =========================================================================
    // Parameters - Emission
    // =========================================================================

    /// Particles emitted per second (0-10000)
    Param<float> emitRate{"emitRate", 500.0f, 0.0f, 10000.0f};

    /// Maximum particle count (buffer capacity)
    Param<int> maxParticles{"maxParticles", 100000, 1000, 1000000};

    /// Emitter position in normalized 0-1 space
    Vec2Param emitterPosition{"emitterPosition", 0.5f, 0.5f, 0.0f, 1.0f};

    /// Emitter size (radius for disc/ring, width for line/rect)
    Param<float> emitterSize{"emitterSize", 0.1f, 0.0f, 1.0f};

    // =========================================================================
    // Parameters - Particle Properties
    // =========================================================================

    /// Minimum particle lifetime in seconds
    Param<float> lifeMin{"lifeMin", 1.0f, 0.1f, 10.0f};

    /// Maximum particle lifetime in seconds
    Param<float> lifeMax{"lifeMax", 3.0f, 0.1f, 10.0f};

    /// Particle size at birth
    Param<float> sizeStart{"sizeStart", 0.015f, 0.001f, 0.2f};

    /// Particle size at death
    Param<float> sizeEnd{"sizeEnd", 0.005f, 0.001f, 0.2f};

    /// Initial velocity direction and magnitude
    Vec2Param initialVelocity{"initialVelocity", 0.0f, 0.0f, -1.0f, 1.0f};

    /// Velocity spread/randomness
    Param<float> velocitySpread{"velocitySpread", 0.1f, 0.0f, 2.0f};

    // =========================================================================
    // Parameters - Curl Noise Force Field
    // =========================================================================

    /// Curl noise force strength (0 = disabled)
    Param<float> curlStrength{"curlStrength", 0.5f, 0.0f, 5.0f};

    /// Curl noise scale (frequency)
    Param<float> curlScale{"curlScale", 4.0f, 0.1f, 20.0f};

    /// Curl noise animation speed
    Param<float> curlSpeed{"curlSpeed", 0.3f, 0.0f, 2.0f};

    /// Curl noise FBM octaves
    Param<int> curlOctaves{"curlOctaves", 3, 1, 6};

    // =========================================================================
    // Parameters - Vortex Force Field
    // =========================================================================

    /// Vortex rotation strength (negative = clockwise)
    Param<float> vortexStrength{"vortexStrength", 0.0f, -5.0f, 5.0f};

    /// Vortex center position
    Vec2Param vortexCenter{"vortexCenter", 0.5f, 0.5f, 0.0f, 1.0f};

    /// Vortex falloff distance
    Param<float> vortexFalloff{"vortexFalloff", 0.2f, 0.01f, 1.0f};

    // =========================================================================
    // Parameters - Gravity
    // =========================================================================

    /// Constant gravity force
    Vec2Param gravity{"gravity", 0.0f, 0.0f, -2.0f, 2.0f};

    /// Velocity damping (drag)
    Param<float> drag{"drag", 0.0f, 0.0f, 5.0f};

    // =========================================================================
    // Parameters - Color
    // =========================================================================

    /// Start color (at birth)
    ColorParam colorStart{"colorStart", 1.0f, 0.6f, 0.2f, 1.0f};

    /// End color (at death, for gradient mode)
    ColorParam colorEnd{"colorEnd", 1.0f, 0.0f, 0.0f, 0.0f};

    /// Enable alpha fade-out at death
    Param<bool> fadeOut{"fadeOut", true, false, true};

    // =========================================================================
    // Parameters - Background
    // =========================================================================

    /// Background clear color
    ColorParam clearColor{"clearColor", 0.0f, 0.0f, 0.0f, 1.0f};

    // =========================================================================
    // API Methods
    // =========================================================================

    /// Set emitter shape
    void emitter(GPUEmitterShape shape) { m_emitterShape = shape; }

    /// Set color mode
    void colorMode(GPUColorMode mode) { m_colorMode = mode; }

    /// Emit a burst of particles immediately
    void burst(int count) { m_burstPending += count; }

    /// Set random seed for reproducibility
    void seed(int s) { m_seed = s; m_rng.seed(s); }

    // =========================================================================
    // Operator Interface
    // =========================================================================

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "GPUParticles"; }

    std::vector<ParamDecl> params() override;
    bool getParam(const std::string& name, float out[4]) override;
    bool setParam(const std::string& name, const float value[4]) override;

    // =========================================================================
    // State Accessors
    // =========================================================================

    /// Get current alive particle count
    int particleCount() const { return m_aliveCount; }

private:
    // GPU Buffers (ping-pong for compute)
    BufferHandle m_particleBuffer[2];
    int m_readBufferIndex = 0;
    int m_allocatedParticles = 0;

    // Compute Pipeline
    ComputePipelineHandle m_simulatePipeline;
    BindGroupLayoutHandle m_simulateBindGroupLayout;
    BindGroupHandle m_simulateBindGroup[2]; // One per buffer configuration
    BufferHandle m_simulateUniformBuffer;

    // Render Pipeline (for circles from GPU buffer)
    RenderPipelineHandle m_renderPipeline;
    BindGroupLayoutHandle m_renderBindGroupLayout;
    BindGroupHandle m_renderBindGroup;
    BufferHandle m_renderUniformBuffer;
    BufferHandle m_circleVertexBuffer;
    BufferHandle m_circleIndexBuffer;
    uint32_t m_circleIndexCount = 0;

    // State
    GPUEmitterShape m_emitterShape = GPUEmitterShape::Point;
    GPUColorMode m_colorMode = GPUColorMode::Gradient;
    int m_aliveCount = 0;
    int m_totalEmitted = 0;
    int m_burstPending = 0;
    float m_emitAccumulator = 0.0f;
    float m_time = 0.0f;
    int m_seed = 42;
    std::mt19937 m_rng{42};
    int m_particleIndex = 0;

    // Staging buffer for CPU->GPU particle emission
    std::vector<GPUParticle> m_emissionStaging;

    // Helper methods
    void createBuffers(WGPUDevice device, WGPUQueue queue);
    void createComputePipeline(WGPUDevice device);
    void createRenderPipeline(WGPUDevice device);
    void createCircleMesh(WGPUDevice device);
    void emitParticles(WGPUDevice device, WGPUQueue queue, float dt);
    void dispatchSimulation(Context& ctx, float dt);
    void renderParticles(Context& ctx);

    glm::vec2 getEmitterPosition();
    glm::vec2 getInitialVelocity();
    glm::vec4 getParticleColor();
};

} // namespace vivid::effects
