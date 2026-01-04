#pragma once

// Vivid Effects - Unified Particle System
// Configurable particle system with multiple simulation backends and render modes
//
// Replaces: Particles (2D CPU), GPUParticles (2D GPU), Particles3D (3D CPU)
//
// Key features:
// - SimulationMode: CPU (prototyping, <50K) or GPU (production, 500K+)
// - ParticleSpace: Screen2D (0-1 coordinates) or World3D (world units)
// - RenderMode: Circle, Sprite, Billboard, Mesh (instanced)
// - Full Param<T> support for MCP slider integration

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <random>
#include <string>
#include <functional>

namespace vivid::effects {

// =============================================================================
// Enums
// =============================================================================

/// Particle simulation backend
enum class SimulationMode {
    CPU,    ///< CPU-based simulation - flexible, debuggable, <50K particles
    GPU     ///< GPU compute shader - high performance, 500K+ particles
};

/// Coordinate space for particles
enum class ParticleSpace {
    Screen2D,   ///< 2D normalized coordinates (0-1), no camera required
    World3D     ///< 3D world-space coordinates, camera required for rendering
};

/// Particle rendering mode
enum class RenderMode {
    Circle,     ///< SDF circles (fastest, no texture needed)
    Sprite,     ///< Textured quads (2D or camera-facing)
    Billboard,  ///< Camera-facing quads in 3D (requires camera)
    Mesh        ///< Instanced 3D mesh (requires camera and mesh input)
};

/// Emitter shape (unified 2D/3D)
enum class PsEmitterShape {
    Point,      ///< Single point emitter
    Line,       ///< Line segment (2D) or line in 3D
    Ring,       ///< Circle outline (2D) or ring in 3D
    Disc,       ///< Filled circle (2D) or flat disc in 3D
    Rectangle,  ///< Rectangle area (2D only)
    Sphere,     ///< Sphere surface or volume (3D only)
    Box,        ///< Box volume (3D only)
    Cone        ///< Cone volume (3D only, good for jets/flames)
};

/// Color interpolation mode
enum class PsColorMode {
    Solid,      ///< Single color throughout lifetime
    Gradient,   ///< Interpolate start to end color over lifetime
    Rainbow,    ///< HSV rainbow based on particle index
    Random      ///< Random color per particle at spawn
};

// =============================================================================
// Particle Data Structure
// =============================================================================

/// Unified particle data (works for 2D and 3D)
struct Particle {
    glm::vec3 position{0.0f};       ///< Position (z=0 for 2D)
    glm::vec3 velocity{0.0f};       ///< Velocity (z=0 for 2D)
    float life = 0.0f;              ///< Remaining lifetime
    float maxLife = 1.0f;           ///< Initial lifetime (for age calculation)
    float size = 1.0f;              ///< Current size
    float rotation = 0.0f;          ///< Rotation angle (radians)
    float angularVelocity = 0.0f;   ///< Spin speed (radians/sec)
    glm::vec4 color{1.0f};          ///< Current color with alpha
    int index = 0;                  ///< Particle index (for rainbow mode)
    int frameOffset = 0;            ///< Random spritesheet frame offset
    float seed = 0.0f;              ///< Per-particle random seed
};

// =============================================================================
// ParticleSystem Operator
// =============================================================================

/**
 * @brief Unified particle system with configurable simulation and rendering
 *
 * @par Example - 2D Flow Field
 * @code
 * auto& ps = chain.add<ParticleSystem>("flow");
 * ps.space(ParticleSpace::Screen2D);
 * ps.rendering(RenderMode::Circle);
 * ps.emitRate = 1000.0f;
 * ps.curlStrength = 0.8f;
 * ps.colorStart.set(1.0f, 0.6f, 0.2f, 1.0f);
 * @endcode
 *
 * @par Example - 3D Velocity-Aligned Meshes
 * @code
 * auto& ps = chain.add<ParticleSystem>("debris");
 * ps.space(ParticleSpace::World3D);
 * ps.rendering(RenderMode::Mesh);
 * ps.simulation(SimulationMode::CPU);
 * ps.setMesh(&elongatedCube);
 * ps.setCameraInput(&camera);
 * ps.alignToVelocity(true);
 * ps.gravity = {0.0f, -9.8f, 0.0f};
 * @endcode
 */
class ParticleSystem : public TextureOperator {
public:
    ParticleSystem();
    ~ParticleSystem() override;

    // =========================================================================
    // Mode Configuration
    // =========================================================================

    /// Set simulation backend (CPU or GPU)
    void simulation(SimulationMode mode) { m_simulationMode = mode; markDirty(); }
    SimulationMode simulation() const { return m_simulationMode; }

    /// Set coordinate space (Screen2D or World3D)
    void space(ParticleSpace space) { m_particleSpace = space; markDirty(); }
    ParticleSpace space() const { return m_particleSpace; }

    /// Set rendering mode
    void rendering(RenderMode mode) { m_renderMode = mode; markDirty(); }
    RenderMode rendering() const { return m_renderMode; }

    // =========================================================================
    // Parameters - Emission
    // =========================================================================

    /// Particles emitted per second
    Param<float> emitRate{"emitRate", 100.0f, 0.0f, 10000.0f};

    /// Maximum particle count (buffer capacity)
    Param<int> maxParticles{"maxParticles", 10000, 100, 1000000};

    /// Emitter position (z ignored for 2D)
    Vec3Param emitterPosition{"emitterPosition", 0.5f, 0.5f, 0.0f, -10.0f, 10.0f};

    /// Emitter size (radius for disc/ring/sphere, half-extent for rect/box)
    Param<float> emitterSize{"emitterSize", 0.1f, 0.0f, 10.0f};

    /// Emitter direction (for cone shape)
    Vec3Param emitterDirection{"emitterDirection", 0.0f, 1.0f, 0.0f, -1.0f, 1.0f};

    /// Cone angle in degrees (for cone emitter)
    Param<float> coneAngle{"coneAngle", 30.0f, 0.0f, 180.0f};

    // =========================================================================
    // Parameters - Particle Properties
    // =========================================================================

    /// Minimum particle lifetime in seconds
    Param<float> lifeMin{"lifeMin", 1.0f, 0.1f, 30.0f};

    /// Maximum particle lifetime in seconds
    Param<float> lifeMax{"lifeMax", 3.0f, 0.1f, 30.0f};

    /// Particle size at birth
    Param<float> sizeStart{"sizeStart", 0.02f, 0.001f, 1.0f};

    /// Particle size at death
    Param<float> sizeEnd{"sizeEnd", 0.02f, 0.001f, 1.0f};

    /// Size variation (0-1 multiplier)
    Param<float> sizeVariation{"sizeVariation", 0.0f, 0.0f, 1.0f};

    // =========================================================================
    // Parameters - Initial Velocity
    // =========================================================================

    /// Base velocity direction (z ignored for 2D)
    Vec3Param initialVelocity{"initialVelocity", 0.0f, 0.0f, 0.0f, -5.0f, 5.0f};

    /// Velocity along emitter normal/outward direction
    Param<float> radialVelocity{"radialVelocity", 0.0f, -5.0f, 5.0f};

    /// Spread angle in degrees
    Param<float> spread{"spread", 0.0f, 0.0f, 360.0f};

    /// Velocity magnitude variation (0-1)
    Param<float> velocityVariation{"velocityVariation", 0.0f, 0.0f, 1.0f};

    // =========================================================================
    // Parameters - Physics
    // =========================================================================

    /// Gravity force (z ignored for 2D)
    Vec3Param gravity{"gravity", 0.0f, 0.0f, 0.0f, -20.0f, 20.0f};

    /// Velocity damping (0 = no drag)
    Param<float> drag{"drag", 0.0f, 0.0f, 5.0f};

    /// Random turbulence strength
    Param<float> turbulence{"turbulence", 0.0f, 0.0f, 2.0f};

    /// Attractor position (z ignored for 2D)
    Vec3Param attractorPosition{"attractorPosition", 0.5f, 0.5f, 0.0f, -10.0f, 10.0f};

    /// Attractor strength (negative = repel)
    Param<float> attractorStrength{"attractorStrength", 0.0f, -10.0f, 10.0f};

    // =========================================================================
    // Parameters - Curl Noise Force Field
    // =========================================================================

    /// Curl noise force strength (0 = disabled)
    Param<float> curlStrength{"curlStrength", 0.0f, 0.0f, 5.0f};

    /// Curl noise scale (frequency)
    Param<float> curlScale{"curlScale", 4.0f, 0.1f, 20.0f};

    /// Curl noise animation speed
    Param<float> curlSpeed{"curlSpeed", 0.3f, 0.0f, 2.0f};

    /// Curl noise FBM octaves
    Param<int> curlOctaves{"curlOctaves", 3, 1, 6};

    // =========================================================================
    // Parameters - Color
    // =========================================================================

    /// Start color (at birth)
    ColorParam colorStart{"colorStart", 1.0f, 0.6f, 0.2f, 1.0f};

    /// End color (at death, for gradient mode)
    ColorParam colorEnd{"colorEnd", 1.0f, 0.0f, 0.0f, 0.0f};

    /// Fade in time (seconds)
    Param<float> fadeInTime{"fadeInTime", 0.0f, 0.0f, 2.0f};

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
    void emitter(PsEmitterShape shape) { m_emitterShape = shape; }
    PsEmitterShape emitter() const { return m_emitterShape; }

    /// Set color mode
    void colorMode(PsColorMode mode) { m_colorMode = mode; }
    PsColorMode colorMode() const { return m_colorMode; }

    /// Emit a burst of particles immediately
    void burst(int count) { m_burstPending += count; }

    /// Set random seed for reproducibility
    void seed(int s) { m_seed = s; m_rng.seed(s); }

    // =========================================================================
    // Texture/Sprite Settings
    // =========================================================================

    /// Load sprite texture (enables sprite/billboard mode rendering)
    void texture(const std::string& path) { m_texturePath = path; m_useTexture = true; }

    /// Spin speed (radians per second)
    void spin(float speed) { m_spinSpeed = speed; }

    /// Configure spritesheet grid (cols x rows)
    void spriteSheet(int cols, int rows);

    /// Set total frame count (if less than cols * rows)
    void spriteFrames(int count) { m_spriteFrameCount = count; }

    /// Animate sprite frame based on particle lifetime
    void spriteAnimateByLife(bool enable) { m_spriteAnimateByLife = enable; }

    /// Frame rate for time-based sprite animation
    void spriteFPS(float fps) { m_spriteFPS = fps; }

    // =========================================================================
    // 3D Camera (required for Billboard and Mesh modes)
    // =========================================================================

    /// Set camera matrices for 3D rendering (call each frame if camera moves)
    void setCamera(const glm::mat4& viewMatrix, const glm::mat4& projMatrix,
                   const glm::vec3& cameraRight, const glm::vec3& cameraUp) {
        m_viewMatrix = viewMatrix;
        m_projMatrix = projMatrix;
        m_cameraRight = cameraRight;
        m_cameraUp = cameraUp;
        m_hasCamera = true;
    }

    /// Set camera matrices from view-projection combined
    void setCamera(const glm::mat4& viewProj, const glm::vec3& cameraRight,
                   const glm::vec3& cameraUp) {
        m_viewProjMatrix = viewProj;
        m_cameraRight = cameraRight;
        m_cameraUp = cameraUp;
        m_hasCamera = true;
        m_hasViewProj = true;
    }

    // =========================================================================
    // 3D Mesh Rendering (RenderMode::Mesh only)
    // =========================================================================

    /// Set mesh vertex/index buffers for instanced rendering (advanced)
    void setMeshBuffers(WGPUBuffer vertexBuffer, WGPUBuffer indexBuffer,
                        uint32_t indexCount, uint32_t vertexStride) {
        m_meshVertexBuffer = vertexBuffer;
        m_meshIndexBuffer = indexBuffer;
        m_meshIndexCount = indexCount;
        m_meshVertexStride = vertexStride;
        m_useBuiltinMesh = false;
    }

    /// Use built-in elongated cube mesh (good for velocity-aligned effects)
    /// Call this before first process() if you want the default cube mesh
    void useBuiltinCube(float width = 0.02f, float length = 0.1f) {
        m_builtinCubeWidth = width;
        m_builtinCubeLength = length;
        m_useBuiltinMesh = true;
    }

    /// Align mesh orientation to velocity vector
    void alignToVelocity(bool enable) { m_alignToVelocity = enable; }

    // =========================================================================
    // Rendering Options
    // =========================================================================

    /// Enable additive blending (good for fire, sparks)
    void additive(bool enable) { m_additiveBlend = enable; }

    /// Enable depth sorting (slower but correct transparency)
    void depthSort(bool enable) { m_depthSort = enable; }

    /// Enable depth testing against scene
    void depthTest(bool enable) { m_depthTest = enable; }

    // =========================================================================
    // Operator Interface
    // =========================================================================

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "ParticleSystem"; }

    std::vector<ParamDecl> params() override;
    bool getParam(const std::string& name, float out[4]) override;
    bool setParam(const std::string& name, const float value[4]) override;

    // =========================================================================
    // State Accessors
    // =========================================================================

    /// Get current alive particle count
    int particleCount() const { return static_cast<int>(m_particles.size()); }

    /// Get particle positions (for plexus/connection effects)
    std::vector<glm::vec3> getPositions() const;

    /// Custom force callback (called per-particle in CPU mode)
    void setForceCallback(std::function<glm::vec3(const Particle&, float time)> cb) {
        m_forceCallback = std::move(cb);
    }

private:
    // -------------------------------------------------------------------------
    // Mode Configuration
    // -------------------------------------------------------------------------
    SimulationMode m_simulationMode = SimulationMode::CPU;
    ParticleSpace m_particleSpace = ParticleSpace::Screen2D;
    RenderMode m_renderMode = RenderMode::Circle;

    // -------------------------------------------------------------------------
    // Emitter State
    // -------------------------------------------------------------------------
    PsEmitterShape m_emitterShape = PsEmitterShape::Point;
    PsColorMode m_colorMode = PsColorMode::Gradient;
    float m_emitAccumulator = 0.0f;
    int m_burstPending = 0;
    int m_particleIndex = 0;

    // -------------------------------------------------------------------------
    // Texture/Sprite State
    // -------------------------------------------------------------------------
    std::string m_texturePath;
    bool m_useTexture = false;
    float m_spinSpeed = 0.0f;
    bool m_useSpriteSheet = false;
    int m_spriteSheetCols = 1;
    int m_spriteSheetRows = 1;
    int m_spriteFrameCount = 1;
    bool m_spriteAnimateByLife = true;
    float m_spriteFPS = 30.0f;

    // -------------------------------------------------------------------------
    // 3D Camera State
    // -------------------------------------------------------------------------
    glm::mat4 m_viewMatrix{1.0f};
    glm::mat4 m_projMatrix{1.0f};
    glm::mat4 m_viewProjMatrix{1.0f};
    glm::vec3 m_cameraRight{1.0f, 0.0f, 0.0f};
    glm::vec3 m_cameraUp{0.0f, 1.0f, 0.0f};
    bool m_hasCamera = false;
    bool m_hasViewProj = false;

    // -------------------------------------------------------------------------
    // 3D Mesh Rendering State
    // -------------------------------------------------------------------------
    WGPUBuffer m_meshVertexBuffer = nullptr;
    WGPUBuffer m_meshIndexBuffer = nullptr;
    uint32_t m_meshIndexCount = 0;
    uint32_t m_meshVertexStride = 0;
    bool m_useBuiltinMesh = true;
    float m_builtinCubeWidth = 0.02f;
    float m_builtinCubeLength = 0.1f;
    bool m_builtinMeshCreated = false;
    bool m_builtinMeshUploaded = false;
    std::vector<uint8_t> m_builtinVertexData;
    std::vector<uint8_t> m_builtinIndexData;
    bool m_alignToVelocity = false;
    bool m_additiveBlend = false;
    bool m_depthSort = true;
    bool m_depthTest = false;

    // -------------------------------------------------------------------------
    // Random State
    // -------------------------------------------------------------------------
    int m_seed = 42;
    std::mt19937 m_rng{42};

    // -------------------------------------------------------------------------
    // Particle Storage (CPU mode)
    // -------------------------------------------------------------------------
    std::vector<Particle> m_particles;
    std::vector<size_t> m_sortedIndices;

    // -------------------------------------------------------------------------
    // Custom Force Callback
    // -------------------------------------------------------------------------
    std::function<glm::vec3(const Particle&, float time)> m_forceCallback;

    // -------------------------------------------------------------------------
    // GPU Resources
    // -------------------------------------------------------------------------
    // Circle/Sprite renderer (2D)
    class ParticleRenderer* m_renderer2D = nullptr;

    // Billboard renderer (3D camera-facing quads)
    WGPURenderPipeline m_billboardPipeline = nullptr;
    WGPUBindGroupLayout m_billboardBindGroupLayout = nullptr;
    WGPUBuffer m_billboardUniformBuffer = nullptr;
    WGPUBuffer m_billboardInstanceBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;
    WGPUTexture m_spriteTexture = nullptr;
    WGPUTextureView m_spriteTextureView = nullptr;
    size_t m_billboardInstanceCapacity = 0;

    // Mesh instanced renderer (3D meshes)
    WGPURenderPipeline m_meshPipeline = nullptr;
    WGPUBindGroupLayout m_meshBindGroupLayout = nullptr;
    WGPUBuffer m_meshUniformBuffer = nullptr;
    WGPUBuffer m_meshInstanceBuffer = nullptr;
    size_t m_meshInstanceCapacity = 0;

    // GPU compute (GPU simulation mode)
    WGPUBuffer m_particleBufferGPU[2] = {nullptr, nullptr};
    int m_readBufferIndex = 0;
    WGPUComputePipeline m_computePipeline = nullptr;
    WGPUBindGroupLayout m_computeBindGroupLayout = nullptr;
    WGPUBindGroup m_computeBindGroup[2] = {nullptr, nullptr};
    WGPUBuffer m_computeUniformBuffer = nullptr;
    int m_allocatedParticlesGPU = 0;
    int m_aliveCountGPU = 0;
    float m_time = 0.0f;

    // GPU circle rendering (direct from particle storage buffer)
    WGPURenderPipeline m_gpuCirclePipeline = nullptr;
    WGPUBindGroupLayout m_gpuCircleBindGroupLayout = nullptr;
    WGPUBuffer m_gpuCircleUniformBuffer = nullptr;
    WGPUBuffer m_gpuCircleVertexBuffer = nullptr;
    WGPUBuffer m_gpuCircleIndexBuffer = nullptr;
    uint32_t m_gpuCircleIndexCount = 0;

    // GPU billboard rendering (direct from particle storage buffer)
    WGPURenderPipeline m_gpuBillboardPipeline = nullptr;
    WGPUBindGroupLayout m_gpuBillboardBindGroupLayout = nullptr;
    WGPUBuffer m_gpuBillboardUniformBuffer = nullptr;

    // GPU mesh rendering (direct from particle storage buffer)
    WGPURenderPipeline m_gpuMeshPipeline = nullptr;
    WGPUBindGroupLayout m_gpuMeshBindGroupLayout = nullptr;
    WGPUBuffer m_gpuMeshUniformBuffer = nullptr;

    // -------------------------------------------------------------------------
    // Helper Methods - Simulation
    // -------------------------------------------------------------------------
    void emitParticle();
    glm::vec3 getEmitterPosition();
    glm::vec3 getInitialVelocity(const glm::vec3& spawnPos);
    glm::vec4 getSpawnColor();
    void updateParticlesCPU(float dt);
    glm::vec3 computeCurlNoise(const glm::vec3& pos, float time);
    glm::vec4 computeParticleColor(const Particle& p, float age);
    float computeParticleSize(const Particle& p, float age);
    static glm::vec4 hsvToRgb(float h, float s, float v);

    // -------------------------------------------------------------------------
    // Helper Methods - GPU Simulation
    // -------------------------------------------------------------------------
    void initGPUBuffers(WGPUDevice device);
    void createComputePipeline(WGPUDevice device);
    void emitParticlesGPU(WGPUDevice device, WGPUQueue queue, float dt);
    void dispatchComputeSimulation(Context& ctx, float dt);
    void readbackParticleCount(WGPUDevice device, WGPUQueue queue);
    void syncGPUToCPU(WGPUDevice device, WGPUQueue queue);

    // -------------------------------------------------------------------------
    // Helper Methods - GPU Circle Rendering
    // -------------------------------------------------------------------------
    void createGPUCirclePipeline(WGPUDevice device);
    void createGPUCircleMesh(WGPUDevice device);
    void renderCirclesGPU(Context& ctx);

    // -------------------------------------------------------------------------
    // Helper Methods - GPU Billboard Rendering
    // -------------------------------------------------------------------------
    void createGPUBillboardPipeline(WGPUDevice device);
    void renderBillboardsGPU(Context& ctx);

    // -------------------------------------------------------------------------
    // Helper Methods - GPU Mesh Rendering
    // -------------------------------------------------------------------------
    void createGPUMeshPipeline(WGPUDevice device);
    void renderMeshesGPU(Context& ctx);

    // -------------------------------------------------------------------------
    // Helper Methods - Rendering
    // -------------------------------------------------------------------------
    void initRenderer(Context& ctx);
    void loadTexture(Context& ctx);
    void renderCircles(Context& ctx);
    void renderSprites(Context& ctx);
    void renderBillboards(Context& ctx);
    void renderMeshes(Context& ctx);
    void sortByDepth(const glm::mat4& viewMatrix);
    void ensureInstanceCapacity(WGPUDevice device, size_t count);
    void ensureMeshInstanceCapacity(WGPUDevice device, size_t count);
    void createBillboardPipeline(WGPUDevice device);
    void createMeshPipeline(WGPUDevice device);
    void createBuiltinCubeMesh(WGPUDevice device);
};

} // namespace vivid::effects
