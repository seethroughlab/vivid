#pragma once

// Vivid Render3D - Particles3D Operator
// 3D GPU particle system with world-space physics and billboard rendering

#include <vivid/operator.h>
#include <vivid/effects/texture_operator.h>
#include <vivid/render3d/camera.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <random>
#include <string>

namespace vivid::render3d {

// Forward declaration
class CameraOperator;

enum class Emitter3DShape {
    Point,      // Single point in 3D space
    Sphere,     // Sphere surface or volume
    Box,        // Box volume
    Cone,       // Cone volume (good for jets, flames)
    Disc        // Flat disc (horizontal plane)
};

enum class Color3DMode {
    Solid,      // Single color
    Gradient,   // Interpolate start to end color over lifetime
    Rainbow,    // HSV rainbow based on particle index
    Random      // Random color per particle
};

class Particles3D : public effects::TextureOperator {
public:
    Particles3D();
    ~Particles3D() override;

    // -------------------------------------------------------------------------
    // Emitter Configuration
    // -------------------------------------------------------------------------

    /// Set emitter shape
    void emitter(Emitter3DShape s) { m_emitterShape = s; markDirty(); }

    /// Set emitter world position
    void position(float x, float y, float z) { m_emitterPos = {x, y, z}; }
    void position(const glm::vec3& p) { m_emitterPos = p; }

    /// Set emitter size (radius for sphere/disc, half-extents for box, cone radius)
    void emitterSize(float s) { m_emitterSize = s; }
    void emitterSize(float x, float y, float z) { m_emitterSizeVec = {x, y, z}; }

    /// Set emitter direction (for cone shape)
    void emitterDirection(float x, float y, float z) { m_emitterDir = glm::normalize(glm::vec3(x, y, z)); }

    /// Set cone angle in degrees (for cone emitter)
    void coneAngle(float degrees) { m_coneAngle = glm::radians(degrees); }

    // -------------------------------------------------------------------------
    // Emission Settings
    // -------------------------------------------------------------------------

    /// Particles emitted per second
    void emitRate(float r) { m_emitRate = r; }

    /// Maximum particle count
    void maxParticles(int m) { m_maxParticles = m; }

    /// Emit a burst of particles immediately
    void burst(int count) { m_burstCount = count; m_needsBurst = true; }

    // -------------------------------------------------------------------------
    // Initial Velocity
    // -------------------------------------------------------------------------

    /// Base velocity direction
    void velocity(float x, float y, float z) { m_baseVelocity = {x, y, z}; }
    void velocity(const glm::vec3& v) { m_baseVelocity = v; }

    /// Velocity along emitter normal/outward direction
    void radialVelocity(float v) { m_radialVelocity = v; }

    /// Spread angle in degrees (cone of possible directions)
    void spread(float degrees) { m_spread = glm::radians(degrees); }

    /// Random velocity magnitude variation (0-1)
    void velocityVariation(float v) { m_velocityVariation = v; }

    // -------------------------------------------------------------------------
    // Physics
    // -------------------------------------------------------------------------

    /// World-space gravity (typically {0, -9.8, 0})
    void gravity(float x, float y, float z) { m_gravity = {x, y, z}; }
    void gravity(const glm::vec3& g) { m_gravity = g; }

    /// Velocity damping (0 = no drag, 1 = full stop)
    void drag(float d) { m_drag = d; }

    /// Random turbulence strength
    void turbulence(float t) { m_turbulence = t; }

    /// Point attractor/repeller
    void attractor(float x, float y, float z, float strength) {
        m_attractorPos = {x, y, z};
        m_attractorStrength = strength;
    }

    // -------------------------------------------------------------------------
    // Lifetime
    // -------------------------------------------------------------------------

    /// Base particle lifetime in seconds
    void life(float l) { m_baseLife = l; }

    /// Random lifetime variation (0-1)
    void lifeVariation(float v) { m_lifeVariation = v; }

    // -------------------------------------------------------------------------
    // Size (billboard size in world units)
    // -------------------------------------------------------------------------

    /// Fixed size
    void size(float s) { m_sizeStart = s; m_sizeEnd = s; }

    /// Size over lifetime (start to end)
    void size(float start, float end) { m_sizeStart = start; m_sizeEnd = end; }

    /// Random size variation (0-1)
    void sizeVariation(float v) { m_sizeVariation = v; }

    // -------------------------------------------------------------------------
    // Color
    // -------------------------------------------------------------------------

    /// Start color
    void color(float r, float g, float b, float a = 1.0f) {
        m_colorStart = {r, g, b, a};
    }
    void color(const glm::vec4& c) { m_colorStart = c; }

    /// End color (enables gradient mode)
    void colorEnd(float r, float g, float b, float a = 1.0f) {
        m_colorEnd = {r, g, b, a};
        m_colorMode = Color3DMode::Gradient;
    }
    void colorEnd(const glm::vec4& c) { m_colorEnd = c; m_colorMode = Color3DMode::Gradient; }

    /// Color mode
    void colorMode(Color3DMode m) { m_colorMode = m; }

    /// Fade in time (seconds)
    void fadeIn(float t) { m_fadeInTime = t; }

    /// Enable fade out at end of life
    void fadeOut(bool enable) { m_fadeOut = enable; }

    // -------------------------------------------------------------------------
    // Texture (sprite mode)
    // -------------------------------------------------------------------------

    /// Load sprite texture (enables sprite mode instead of circles)
    void texture(const std::string& path) { m_texturePath = path; m_useSprites = true; }

    /// Spin speed (radians per second)
    void spin(float speed) { m_spinSpeed = speed; }

    // -------------------------------------------------------------------------
    // Spritesheet Animation
    // -------------------------------------------------------------------------

    /// Configure spritesheet grid (cols x rows)
    void spriteSheet(int cols, int rows) {
        m_spriteSheetCols = cols;
        m_spriteSheetRows = rows;
        m_spriteFrameCount = cols * rows;
        m_useSpriteSheet = true;
    }

    /// Set total frame count (if less than cols * rows)
    void spriteFrames(int count) { m_spriteFrameCount = count; }

    /// Animate sprite frame based on particle lifetime (0-1 maps to frame 0 to N)
    void spriteAnimateByLife(bool enable) { m_spriteAnimateByLife = enable; }

    /// Frame rate for time-based sprite animation (if not using lifetime)
    void spriteFPS(float fps) { m_spriteFPS = fps; }

    /// Random starting frame offset per particle
    void spriteRandomStart(bool enable) { m_spriteRandomStart = enable; }

    // -------------------------------------------------------------------------
    // Rendering
    // -------------------------------------------------------------------------

    /// Clear color (background)
    void clearColor(float r, float g, float b, float a = 1.0f) {
        m_clearColor = {r, g, b, a};
    }

    /// Enable additive blending (good for fire, sparks)
    void additive(bool enable) { m_additiveBlend = enable; }

    /// Enable depth sorting (slower but correct transparency)
    void depthSort(bool enable) { m_depthSort = enable; }

    /// Enable depth testing against scene
    void depthTest(bool enable) { m_depthTest = enable; }

    // -------------------------------------------------------------------------
    // Camera Input (REQUIRED for billboard orientation)
    // -------------------------------------------------------------------------

    void setCameraInput(CameraOperator* cam) { m_cameraOp = cam; }

    // -------------------------------------------------------------------------
    // Random seed
    // -------------------------------------------------------------------------

    void seed(int s) { m_seed = s; m_rng.seed(s); }

    // -------------------------------------------------------------------------
    // Operator Interface
    // -------------------------------------------------------------------------

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Particles3D"; }

    // State accessors
    int particleCount() const { return static_cast<int>(m_particles.size()); }

private:
    struct Particle3D {
        glm::vec3 position;
        glm::vec3 velocity;
        float life;
        float maxLife;
        float size;
        float rotation;      // Billboard rotation (Z axis in screen space)
        float angularVel;
        glm::vec4 color;
        int index;
        int frameOffset;     // Random starting frame for spritesheet animation
    };

    void emitParticle();
    glm::vec3 getEmitterPosition();
    glm::vec3 getInitialVelocity(const glm::vec3& pos);
    void updateParticles(float dt);
    glm::vec4 getParticleColor(const Particle3D& p, float age);
    glm::vec4 hsvToRgb(float h, float s, float v);
    void loadTexture(Context& ctx);
    void createPipeline(Context& ctx);
    void sortParticlesByDepth(const glm::mat4& viewMatrix);

    // Emitter settings
    Emitter3DShape m_emitterShape = Emitter3DShape::Point;
    glm::vec3 m_emitterPos{0.0f, 0.0f, 0.0f};
    float m_emitterSize = 1.0f;
    glm::vec3 m_emitterSizeVec{1.0f, 1.0f, 1.0f};
    glm::vec3 m_emitterDir{0.0f, 1.0f, 0.0f};
    float m_coneAngle = glm::radians(30.0f);

    // Emission settings
    float m_emitRate = 100.0f;
    int m_maxParticles = 5000;
    int m_burstCount = 0;
    bool m_needsBurst = false;
    float m_emitAccumulator = 0.0f;

    // Velocity settings
    glm::vec3 m_baseVelocity{0.0f, 1.0f, 0.0f};
    float m_radialVelocity = 0.0f;
    float m_spread = 0.0f;
    float m_velocityVariation = 0.2f;

    // Physics settings
    glm::vec3 m_gravity{0.0f, -2.0f, 0.0f};
    float m_drag = 0.0f;
    float m_turbulence = 0.0f;
    glm::vec3 m_attractorPos{0.0f, 0.0f, 0.0f};
    float m_attractorStrength = 0.0f;

    // Lifetime settings
    float m_baseLife = 2.0f;
    float m_lifeVariation = 0.2f;

    // Size settings
    float m_sizeStart = 0.1f;
    float m_sizeEnd = 0.1f;
    float m_sizeVariation = 0.0f;

    // Color settings
    Color3DMode m_colorMode = Color3DMode::Solid;
    glm::vec4 m_colorStart{1.0f, 0.5f, 0.2f, 1.0f};
    glm::vec4 m_colorEnd{1.0f, 0.0f, 0.0f, 0.0f};
    float m_fadeInTime = 0.0f;
    bool m_fadeOut = true;

    // Texture settings
    std::string m_texturePath;
    bool m_useSprites = false;
    float m_spinSpeed = 0.0f;
    WGPUTexture m_spriteTexture = nullptr;
    WGPUTextureView m_spriteTextureView = nullptr;

    // Spritesheet animation settings
    bool m_useSpriteSheet = false;
    int m_spriteSheetCols = 1;
    int m_spriteSheetRows = 1;
    int m_spriteFrameCount = 1;
    bool m_spriteAnimateByLife = true;
    float m_spriteFPS = 30.0f;
    bool m_spriteRandomStart = false;

    // Rendering settings
    glm::vec4 m_clearColor{0.0f, 0.0f, 0.0f, 0.0f};
    bool m_additiveBlend = false;
    bool m_depthSort = true;
    bool m_depthTest = false;

    // Camera
    CameraOperator* m_cameraOp = nullptr;

    // Random state
    int m_seed = 42;
    std::mt19937 m_rng;
    int m_particleIndex = 0;

    // Particle storage
    std::vector<Particle3D> m_particles;
    std::vector<size_t> m_sortedIndices;

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUBuffer m_instanceBuffer = nullptr;
    WGPUSampler m_sampler = nullptr;
};

} // namespace vivid::render3d
