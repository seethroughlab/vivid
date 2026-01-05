#pragma once

// Vivid Effects 2D - Particles Operator
// 2D GPU particle system with emitters, physics, and lifetime

#include <vivid/effects/texture_operator.h>
#include <vivid/effects/types.h>
#include <vivid/effects/particle_renderer.h>
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

class Particles : public TextureOperator {
public:
    Particles();
    ~Particles() override;

    // Emitter shape and position
    void emitter(EmitterShape s) { m_emitterShape = s; }
    void position(float x, float y) { m_emitterPos = {x, y}; }
    void position(const glm::vec2& p) { m_emitterPos = p; }
    void emitterSize(float s) { m_emitterSize = s; }
    void emitterAngle(float a) { m_emitterAngle = a; }

    // Emission settings
    void emitRate(float r) { m_emitRate = r; }
    void maxParticles(int m) { m_maxParticles = m; }
    void burst(int count) { m_burstCount = count; m_needsBurst = true; }

    // Initial velocity
    void velocity(float x, float y) { m_baseVelocity = {x, y}; }
    void velocity(const glm::vec2& v) { m_baseVelocity = v; }
    void radialVelocity(float v) { m_radialVelocity = v; }
    void spread(float degrees) { m_spread = glm::radians(degrees); }
    void velocityVariation(float v) { m_velocityVariation = v; }

    // Physics
    void gravity(float g) { m_gravity = g; }
    void drag(float d) { m_drag = d; }
    void turbulence(float t) { m_turbulence = t; }
    void attractor(float x, float y, float strength) {
        m_attractorPos = {x, y};
        m_attractorStrength = strength;
    }

    // Lifetime
    void life(float l) { m_baseLife = l; }
    void lifeVariation(float v) { m_lifeVariation = v; }

    // Size
    void size(float s) { m_sizeStart = s; m_sizeEnd = s; }
    void size(float start, float end) { m_sizeStart = start; m_sizeEnd = end; }
    void sizeVariation(float v) { m_sizeVariation = v; }

    // Color
    void color(float r, float g, float b, float a = 1.0f) {
        m_colorStart = {r, g, b, a};
    }
    void color(const glm::vec4& c) { m_colorStart = c; }
    void colorEnd(float r, float g, float b, float a = 1.0f) {
        m_colorEnd = {r, g, b, a};
        m_colorMode = ColorMode::Gradient;
    }
    void colorEnd(const glm::vec4& c) { m_colorEnd = c; m_colorMode = ColorMode::Gradient; }
    void colorMode(ColorMode m) { m_colorMode = m; }
    void fadeIn(float t) { m_fadeInTime = t; }
    void fadeOut(bool enable) { m_fadeOut = enable; }

    // Texture (enables sprite mode)
    void texture(const std::string& path) { m_texturePath = path; m_useSprites = true; }
    void spin(float speed) { m_spinSpeed = speed; }

    // Background
    void clearColor(float r, float g, float b, float a = 1.0f) {
        m_clearColor = {r, g, b, a};
    }

    // Random seed
    void seed(int s) { m_seed = s; m_rng.seed(s); }

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

    // Emitter settings
    EmitterShape m_emitterShape = EmitterShape::Point;
    glm::vec2 m_emitterPos{0.5f, 0.5f};
    float m_emitterSize = 0.1f;
    float m_emitterAngle = 0.0f;

    // Emission settings
    float m_emitRate = 50.0f;
    int m_maxParticles = 10000;
    int m_burstCount = 0;
    bool m_needsBurst = false;
    float m_emitAccumulator = 0.0f;

    // Velocity settings
    glm::vec2 m_baseVelocity{0.0f, -0.2f};
    float m_radialVelocity = 0.0f;
    float m_spread = 0.0f;
    float m_velocityVariation = 0.0f;

    // Physics settings
    float m_gravity = 0.1f;
    float m_drag = 0.0f;
    float m_turbulence = 0.0f;
    glm::vec2 m_attractorPos{0.5f, 0.5f};
    float m_attractorStrength = 0.0f;

    // Lifetime settings
    float m_baseLife = 2.0f;
    float m_lifeVariation = 0.2f;

    // Size settings
    float m_sizeStart = 0.02f;
    float m_sizeEnd = 0.02f;
    float m_sizeVariation = 0.0f;

    // Color settings
    ColorMode m_colorMode = ColorMode::Solid;
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

    // Background
    glm::vec4 m_clearColor{0.0f, 0.0f, 0.0f, 1.0f};

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
