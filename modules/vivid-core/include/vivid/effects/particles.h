#pragma once

// Vivid Effects 2D - Particles Operator
// 2D GPU particle system with emitters, physics, and lifetime

#include <vivid/effects/texture_operator.h>
#include <vivid/effects/types.h>
#include <vivid/effects/particle_renderer.h>
#include <vivid/param.h>
#include <vivid/color.h>
#include <vivid/operator_registry.h>
#include <glm/glm.hpp>
#include <vector>
#include <random>
#include <string>

namespace vivid::effects {

enum class EmitterShape {
    Point,      // Single point emitter
    Line,       // Line segment emitter
    Ring,       // Circle outline emitter
    Disc,       // Filled circle emitter
    Rectangle   // Rectangle area emitter
};

enum class ColorMode {
    Solid,      // Single color
    Gradient,   // Interpolate start to end color over lifetime
    Rainbow,    // HSV rainbow based on particle index
    Random      // Random color per particle
};

/**
 * @brief 2D GPU particle system with physics and lifetime
 *
 * Creates dynamic particle effects with configurable emitters, physics,
 * colors, and optional sprite textures. Particles are simulated on CPU
 * and rendered efficiently on GPU.
 *
 * @par Example
 * @code
 * auto& particles = chain.add<Particles>("fire");
 * particles.position.set(0.5f, 0.8f);     // Emit from bottom center
 * particles.emitterShape = EmitterShape::Line;
 * particles.emitterSize = 0.3f;
 * particles.velocity.set(0, -0.3f);       // Float upward
 * particles.gravity = -0.1f;              // Negative = rise
 * particles.life = 2.0f;
 * particles.size = 0.05f;
 * particles.sizeEnd = 0.0f;               // Shrink over lifetime
 * particles.color.set(1, 0.5f, 0.1f);     // Orange
 * particles.colorEnd.set(1, 0, 0, 0);     // Fade to red/transparent
 * particles.emitRate = 100;
 * @endcode
 *
 * @see PointSprites, Plexus, Noise
 */
class Particles : public TextureOperator {
public:
    // -------------------------------------------------------------------------
    /// @name Self-Description
    /// @{

    static OperatorDescriptor describe() {
        return OperatorDescriptor("Particles", "Particles", "2D particle system")
            .withUsage(
                "auto& p = chain.add<Particles>(\"fire\");\n"
                "p.position.set(0.5f, 0.8f);  // Emitter position\n"
                "p.emitterShape = EmitterShape::Line;  // Point, Line, Ring, Disc, Rectangle\n"
                "p.velocity.set(0.0f, -0.3f);  // Direction\n"
                "p.gravity = -0.1f;  // Negative = rise\n"
                "p.life = 2.0f;\n"
                "p.emitRate = 100;\n"
                "p.color.set(1.0f, 0.5f, 0.1f, 1.0f);  // Orange\n"
            )
            .withExamples({{"examples/particles"}});
    }

    /// @}

    Particles();
    ~Particles() override;

    // =========================================================================
    // Parameters - Emitter
    // =========================================================================

    /// Emitter shape (Point, Line, Ring, Disc, Rectangle)
    EmitterShape emitterShape = EmitterShape::Point;

    /// Emitter position (normalized 0-1)
    Vec2Param position{"position", 0.5f, 0.5f, 0.0f, 1.0f};

    /// Emitter size (radius for disc/ring, half-width for line/rect)
    Param<float> emitterSize{"emitterSize", 0.1f, 0.0f, 2.0f};

    /// Emitter angle for line emitters (radians)
    Param<float> emitterAngle{"emitterAngle", 0.0f, 0.0f, 6.28f};

    // =========================================================================
    // Parameters - Emission
    // =========================================================================

    /// Particles emitted per second
    Param<float> emitRate{"emitRate", 50.0f, 0.0f, 1000.0f};

    /// Maximum particle count
    Param<int> maxParticles{"maxParticles", 10000, 100, 100000};

    // =========================================================================
    // Parameters - Velocity
    // =========================================================================

    /// Base velocity direction
    Vec2Param velocity{"velocity", 0.0f, -0.2f, -2.0f, 2.0f};

    /// Radial velocity (outward from emitter center)
    Param<float> radialVelocity{"radialVelocity", 0.0f, -2.0f, 2.0f};

    /// Spread angle in degrees
    Param<float> spread{"spread", 0.0f, 0.0f, 360.0f};

    /// Velocity variation (0-1 multiplier)
    Param<float> velocityVariation{"velocityVariation", 0.0f, 0.0f, 1.0f};

    // =========================================================================
    // Parameters - Physics
    // =========================================================================

    /// Gravity acceleration (positive = down)
    Param<float> gravity{"gravity", 0.1f, -2.0f, 2.0f};

    /// Drag coefficient (0 = no drag, 1 = full drag)
    Param<float> drag{"drag", 0.0f, 0.0f, 1.0f};

    /// Turbulence strength
    Param<float> turbulence{"turbulence", 0.0f, 0.0f, 2.0f};

    /// Attractor position
    Vec2Param attractorPosition{"attractorPosition", 0.5f, 0.5f, 0.0f, 1.0f};

    /// Attractor strength
    Param<float> attractorStrength{"attractorStrength", 0.0f, -2.0f, 2.0f};

    // =========================================================================
    // Parameters - Lifetime
    // =========================================================================

    /// Base particle lifetime in seconds
    Param<float> life{"life", 2.0f, 0.1f, 30.0f};

    /// Life variation (0-1 multiplier)
    Param<float> lifeVariation{"lifeVariation", 0.2f, 0.0f, 1.0f};

    // =========================================================================
    // Parameters - Size
    // =========================================================================

    /// Particle size at birth
    Param<float> size{"size", 0.02f, 0.001f, 0.5f};

    /// Particle size at death
    Param<float> sizeEnd{"sizeEnd", 0.02f, 0.001f, 0.5f};

    /// Size variation (0-1 multiplier)
    Param<float> sizeVariation{"sizeVariation", 0.0f, 0.0f, 1.0f};

    // =========================================================================
    // Parameters - Color
    // =========================================================================

    /// Color mode (Solid, Gradient, Rainbow, Random)
    ColorMode colorMode = ColorMode::Solid;

    /// Start color (at birth)
    ColorParam color{"color", 1.0f, 0.5f, 0.2f, 1.0f};

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
    // Methods
    // =========================================================================

    /// Emit a burst of particles immediately
    void burst(int count) { m_burstCount = count; m_needsBurst = true; }

    /// Set random seed for reproducibility
    void seed(int s) { m_seed = s; m_rng.seed(s); }

    /// Set texture path (enables sprite mode)
    void setTexture(const std::string& path) { m_texturePath = path; m_useSprites = true; }

    /// Set spin speed for sprites
    void setSpin(float speed) { m_spinSpeed = speed; }

    // Operator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Particles"; }

    // State accessors
    int particleCount() const { return static_cast<int>(m_particles.size()); }

    /// Get particle positions (normalized 0-1 coordinates) for plexus/connection effects
    std::vector<glm::vec2> getPositions() const {
        std::vector<glm::vec2> positions;
        positions.reserve(m_particles.size());
        for (const auto& p : m_particles) {
            positions.push_back(p.position);
        }
        return positions;
    }

private:
    struct Particle {
        glm::vec2 position;
        glm::vec2 velocity;
        float life;
        float maxLife;
        float size;
        float rotation;
        float angularVel;
        glm::vec4 color;
        int index;
    };

    void emitParticle(const glm::vec2& emitterPos);
    glm::vec2 getEmitterPosition(const glm::vec2& center);
    glm::vec2 getInitialVelocity(const glm::vec2& pos, const glm::vec2& emitterCenter);
    void updateParticles(float dt);
    glm::vec4 getParticleColor(const Particle& p, float age);
    glm::vec4 hsvToRgb(float h, float s, float v);
    void loadTexture(Context& ctx);

    // Emission state
    int m_burstCount = 0;
    bool m_needsBurst = false;
    float m_emitAccumulator = 0.0f;

    // Texture settings
    std::string m_texturePath;
    bool m_useSprites = false;
    float m_spinSpeed = 0.0f;
    WGPUTexture m_spriteTexture = nullptr;
    WGPUTextureView m_spriteTextureView = nullptr;

    // Random state
    int m_seed = 42;
    std::mt19937 m_rng;
    int m_particleIndex = 0;

    // Particle storage
    std::vector<Particle> m_particles;

    // Rendering
    ParticleRenderer m_renderer;
};

} // namespace vivid::effects
