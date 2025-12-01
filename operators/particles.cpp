// Particles Operator
// 2D particle system with emitters, physics, and rendering

#include <vivid/vivid.h>
#include <cmath>
#include <random>
#include <algorithm>

using namespace vivid;

/**
 * @brief 2D particle system operator.
 *
 * Emits, updates, and renders particles with customizable behavior.
 * Supports various emitter shapes, physics forces, and rendering styles.
 *
 * Usage:
 * @code
 * // Simple fountain
 * chain.add<Particles>("fountain")
 *     .emitter(Particles::Point)
 *     .position(0.5f, 0.9f)
 *     .emitRate(100)
 *     .velocity(0.0f, -0.3f)
 *     .spread(30.0f)
 *     .gravity(0.2f)
 *     .life(2.0f);
 *
 * // Circular burst
 * chain.add<Particles>("burst")
 *     .emitter(Particles::Ring)
 *     .position(0.5f, 0.5f)
 *     .emitRate(50)
 *     .radialVelocity(0.2f)
 *     .life(1.5f)
 *     .fadeOut(true);
 *
 * // Audio-reactive particles
 * chain.add<Particles>("reactive")
 *     .emitRateFrom("audioLevel")  // Rate from another node's value
 *     .colorFrom("audioBands");     // Color from frequency bands
 * @endcode
 */
class Particles : public Operator {
public:
    // Emitter shapes
    enum EmitterShape {
        Point = 0,      // Single point
        Line = 1,       // Horizontal or vertical line
        Ring = 2,       // Circle outline
        Disc = 3,       // Filled circle
        Rectangle = 4   // Rectangle area
    };

    // Color modes
    enum ColorMode {
        Solid = 0,
        Gradient = 1,   // Color changes over life
        Rainbow = 2,    // Based on particle index
        Random = 3
    };

    Particles() = default;

    // Fluent API - Emitter shape and position
    Particles& emitter(EmitterShape s) { emitterShape_ = s; return *this; }
    Particles& emitter(int s) { emitterShape_ = static_cast<EmitterShape>(s); return *this; }
    Particles& position(float x, float y) { emitterPos_ = {x, y}; return *this; }
    Particles& position(const glm::vec2& p) { emitterPos_ = p; return *this; }
    Particles& emitterSize(float s) { emitterSize_ = s; return *this; }
    Particles& emitterAngle(float a) { emitterAngle_ = a; return *this; }

    // Fluent API - Emission settings
    Particles& emitRate(float r) { emitRate_ = r; return *this; }
    Particles& maxParticles(int m) { maxParticles_ = m; return *this; }
    Particles& burst(int count) { burstCount_ = count; needsBurst_ = true; return *this; }

    // Fluent API - Initial velocity
    Particles& velocity(float x, float y) { baseVelocity_ = {x, y}; return *this; }
    Particles& velocity(const glm::vec2& v) { baseVelocity_ = v; return *this; }
    Particles& radialVelocity(float v) { radialVelocity_ = v; return *this; }
    Particles& spread(float degrees) { spread_ = glm::radians(degrees); return *this; }
    Particles& velocityVariation(float v) { velocityVariation_ = v; return *this; }

    // Fluent API - Physics
    Particles& gravity(float g) { gravity_ = g; return *this; }
    Particles& drag(float d) { drag_ = d; return *this; }
    Particles& turbulence(float t) { turbulence_ = t; return *this; }
    Particles& attract(float x, float y, float strength) {
        attractorPos_ = {x, y};
        attractorStrength_ = strength;
        return *this;
    }

    // Fluent API - Lifetime
    Particles& life(float l) { baseLife_ = l; return *this; }
    Particles& lifeVariation(float v) { lifeVariation_ = v; return *this; }

    // Fluent API - Size
    Particles& size(float s) { baseSize_ = s; return *this; }
    Particles& sizeVariation(float v) { sizeVariation_ = v; return *this; }
    Particles& sizeOverLife(float start, float end) {
        sizeStart_ = start;
        sizeEnd_ = end;
        sizeOverLife_ = true;
        return *this;
    }

    // Fluent API - Color
    Particles& color(float r, float g, float b, float a = 1.0f) {
        colorStart_ = {r, g, b, a};
        return *this;
    }
    Particles& colorEnd(float r, float g, float b, float a = 1.0f) {
        colorEnd_ = {r, g, b, a};
        colorMode_ = Gradient;
        return *this;
    }
    Particles& colorMode(ColorMode m) { colorMode_ = m; return *this; }
    Particles& fadeIn(float t) { fadeInTime_ = t; return *this; }
    Particles& fadeOut(bool enable) { fadeOut_ = enable; return *this; }

    // Fluent API - Parameter references (for reactive particles)
    Particles& emitRateFrom(const std::string& node) { emitRateNode_ = node; return *this; }
    Particles& positionFrom(const std::string& node) { positionNode_ = node; return *this; }

    // Fluent API - Background
    Particles& clearColor(float r, float g, float b, float a = 1.0f) {
        clearColor_ = {r, g, b, a};
        return *this;
    }

    // Fluent API - Seed
    Particles& seed(int s) { seed_ = s; rng_.seed(s); return *this; }

    void init(Context& ctx) override {
        output_ = ctx.createTexture();
        particles_.reserve(maxParticles_);
        rng_.seed(seed_);
    }

    void process(Context& ctx) override {
        float dt = ctx.dt();

        // Get reactive parameters
        float currentEmitRate = emitRate_;
        if (!emitRateNode_.empty()) {
            currentEmitRate = ctx.getInputValue(emitRateNode_) * emitRate_;
        }

        glm::vec2 currentPos = emitterPos_;
        if (!positionNode_.empty()) {
            auto pos = ctx.getInputValues(positionNode_);
            if (pos.size() >= 2) {
                currentPos = {pos[0], pos[1]};
            }
        }

        // Handle burst emission
        if (needsBurst_) {
            for (int i = 0; i < burstCount_ && (int)particles_.size() < maxParticles_; i++) {
                emitParticle(currentPos);
            }
            needsBurst_ = false;
        }

        // Continuous emission
        emitAccumulator_ += currentEmitRate * dt;
        while (emitAccumulator_ >= 1.0f && (int)particles_.size() < maxParticles_) {
            emitParticle(currentPos);
            emitAccumulator_ -= 1.0f;
        }

        // Update particles
        updateParticles(dt);

        // Remove dead particles
        particles_.erase(
            std::remove_if(particles_.begin(), particles_.end(),
                [](const Particle& p) { return p.life <= 0.0f; }),
            particles_.end()
        );

        // Build render data
        std::vector<Circle2D> circles;
        circles.reserve(particles_.size());

        for (const auto& p : particles_) {
            float lifeRatio = p.life / p.maxLife;
            float age = 1.0f - lifeRatio;

            // Calculate size
            float size = p.size;
            if (sizeOverLife_) {
                size *= glm::mix(sizeStart_, sizeEnd_, age);
            }

            // Calculate color
            glm::vec4 color = getParticleColor(p, age);

            // Apply fade in/out
            float alpha = color.a;
            if (fadeInTime_ > 0.0f && age < fadeInTime_) {
                alpha *= age / fadeInTime_;
            }
            if (fadeOut_) {
                alpha *= lifeRatio;
            }
            color.a = alpha;

            circles.emplace_back(p.position, size, color);
        }

        ctx.drawCircles(circles, output_, clearColor_);
        ctx.setOutput("out", output_);

        // Output particle count as value
        ctx.setOutput("count", static_cast<float>(particles_.size()));
    }

    std::vector<ParamDecl> params() override {
        return {
            intParam("emitter", static_cast<int>(emitterShape_), 0, 4),
            floatParam("emitRate", emitRate_, 0.0f, 500.0f),
            floatParam("life", baseLife_, 0.1f, 10.0f),
            floatParam("gravity", gravity_, -1.0f, 1.0f),
            floatParam("size", baseSize_, 0.001f, 0.1f),
            floatParam("spread", glm::degrees(spread_), 0.0f, 360.0f)
        };
    }

    OutputKind outputKind() override { return OutputKind::Texture; }

private:
    struct Particle {
        glm::vec2 position;
        glm::vec2 velocity;
        float life;
        float maxLife;
        float size;
        glm::vec4 color;
        int index;
    };

    void emitParticle(const glm::vec2& emitterPos) {
        Particle p;
        p.index = particleIndex_++;

        // Initial position based on emitter shape
        p.position = getEmitterPosition(emitterPos);

        // Initial velocity
        p.velocity = getInitialVelocity(p.position, emitterPos);

        // Lifetime with variation
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        p.maxLife = baseLife_ * (1.0f + lifeVariation_ * dist(rng_));
        p.life = p.maxLife;

        // Size with variation
        p.size = baseSize_ * (1.0f + sizeVariation_ * dist(rng_));

        // Initial color
        p.color = colorStart_;

        particles_.push_back(p);
    }

    glm::vec2 getEmitterPosition(const glm::vec2& center) {
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        std::uniform_real_distribution<float> dist01(0.0f, 1.0f);

        switch (emitterShape_) {
            case Point:
                return center;

            case Line: {
                float offset = dist(rng_) * emitterSize_ * 0.5f;
                float ca = std::cos(emitterAngle_);
                float sa = std::sin(emitterAngle_);
                return center + glm::vec2(offset * ca, offset * sa);
            }

            case Ring: {
                float angle = dist01(rng_) * 2.0f * 3.14159f;
                return center + emitterSize_ * glm::vec2(std::cos(angle), std::sin(angle));
            }

            case Disc: {
                float angle = dist01(rng_) * 2.0f * 3.14159f;
                float radius = std::sqrt(dist01(rng_)) * emitterSize_;
                return center + radius * glm::vec2(std::cos(angle), std::sin(angle));
            }

            case Rectangle:
                return center + glm::vec2(dist(rng_), dist(rng_)) * emitterSize_ * 0.5f;

            default:
                return center;
        }
    }

    glm::vec2 getInitialVelocity(const glm::vec2& pos, const glm::vec2& emitterCenter) {
        glm::vec2 vel = baseVelocity_;

        // Add radial velocity (away from center)
        if (radialVelocity_ != 0.0f) {
            glm::vec2 dir = pos - emitterCenter;
            if (glm::length(dir) > 0.001f) {
                vel += glm::normalize(dir) * radialVelocity_;
            } else {
                // Random direction if at center
                std::uniform_real_distribution<float> dist(0.0f, 2.0f * 3.14159f);
                float angle = dist(rng_);
                vel += radialVelocity_ * glm::vec2(std::cos(angle), std::sin(angle));
            }
        }

        // Apply spread (cone of randomness)
        if (spread_ > 0.0f) {
            std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
            float angle = dist(rng_) * spread_ * 0.5f;
            float ca = std::cos(angle);
            float sa = std::sin(angle);
            vel = glm::vec2(vel.x * ca - vel.y * sa, vel.x * sa + vel.y * ca);
        }

        // Velocity variation
        if (velocityVariation_ > 0.0f) {
            std::uniform_real_distribution<float> dist(1.0f - velocityVariation_, 1.0f + velocityVariation_);
            vel *= dist(rng_);
        }

        return vel;
    }

    void updateParticles(float dt) {
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

        for (auto& p : particles_) {
            // Apply gravity
            p.velocity.y += gravity_ * dt;

            // Apply drag
            if (drag_ > 0.0f) {
                p.velocity *= (1.0f - drag_ * dt);
            }

            // Apply turbulence
            if (turbulence_ > 0.0f) {
                p.velocity += glm::vec2(dist(rng_), dist(rng_)) * turbulence_ * dt;
            }

            // Apply attractor
            if (attractorStrength_ != 0.0f) {
                glm::vec2 toAttractor = attractorPos_ - p.position;
                float distance = glm::length(toAttractor);
                if (distance > 0.01f) {
                    p.velocity += glm::normalize(toAttractor) * attractorStrength_ * dt / distance;
                }
            }

            // Update position
            p.position += p.velocity * dt;

            // Update life
            p.life -= dt;
        }
    }

    glm::vec4 getParticleColor(const Particle& p, float age) {
        switch (colorMode_) {
            case Solid:
                return colorStart_;

            case Gradient:
                return glm::mix(colorStart_, colorEnd_, age);

            case Rainbow: {
                float hue = std::fmod(p.index * 0.1f, 1.0f);
                return hsvToRgb(hue, 0.8f, 1.0f);
            }

            case Random: {
                // Use particle index as seed for consistent random color
                std::mt19937 localRng(p.index);
                std::uniform_real_distribution<float> dist(0.0f, 1.0f);
                return glm::vec4(dist(localRng), dist(localRng), dist(localRng), 1.0f);
            }

            default:
                return colorStart_;
        }
    }

    glm::vec4 hsvToRgb(float h, float s, float v) {
        float c = v * s;
        float x = c * (1.0f - std::abs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
        float m = v - c;

        glm::vec3 rgb;
        if (h < 1.0f/6.0f)      rgb = {c, x, 0};
        else if (h < 2.0f/6.0f) rgb = {x, c, 0};
        else if (h < 3.0f/6.0f) rgb = {0, c, x};
        else if (h < 4.0f/6.0f) rgb = {0, x, c};
        else if (h < 5.0f/6.0f) rgb = {x, 0, c};
        else                    rgb = {c, 0, x};

        return glm::vec4(rgb + glm::vec3(m), 1.0f);
    }

    // Emitter settings
    EmitterShape emitterShape_ = Point;
    glm::vec2 emitterPos_{0.5f, 0.5f};
    float emitterSize_ = 0.1f;
    float emitterAngle_ = 0.0f;

    // Emission settings
    float emitRate_ = 50.0f;
    int maxParticles_ = 5000;
    int burstCount_ = 0;
    bool needsBurst_ = false;
    float emitAccumulator_ = 0.0f;

    // Velocity settings
    glm::vec2 baseVelocity_{0.0f, -0.2f};
    float radialVelocity_ = 0.0f;
    float spread_ = 0.0f;
    float velocityVariation_ = 0.0f;

    // Physics settings
    float gravity_ = 0.1f;
    float drag_ = 0.0f;
    float turbulence_ = 0.0f;
    glm::vec2 attractorPos_{0.5f, 0.5f};
    float attractorStrength_ = 0.0f;

    // Lifetime settings
    float baseLife_ = 2.0f;
    float lifeVariation_ = 0.2f;

    // Size settings
    float baseSize_ = 0.01f;
    float sizeVariation_ = 0.0f;
    bool sizeOverLife_ = false;
    float sizeStart_ = 1.0f;
    float sizeEnd_ = 0.0f;

    // Color settings
    ColorMode colorMode_ = Solid;
    glm::vec4 colorStart_{1.0f, 0.5f, 0.2f, 1.0f};
    glm::vec4 colorEnd_{1.0f, 0.0f, 0.0f, 0.0f};
    float fadeInTime_ = 0.0f;
    bool fadeOut_ = true;

    // Parameter node references
    std::string emitRateNode_;
    std::string positionNode_;

    // Background
    glm::vec4 clearColor_{0.0f, 0.0f, 0.0f, 1.0f};

    // Random state
    int seed_ = 42;
    std::mt19937 rng_;
    int particleIndex_ = 0;

    // Particle storage
    std::vector<Particle> particles_;

    // Output
    Texture output_;
};

VIVID_OPERATOR(Particles)
