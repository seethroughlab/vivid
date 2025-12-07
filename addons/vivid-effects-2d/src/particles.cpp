// Vivid Effects 2D - Particles Operator Implementation
// 2D GPU particle system with emitters, physics, and lifetime

#include <vivid/effects/particles.h>
#include <vivid/context.h>
#include <cmath>
#include <algorithm>

namespace vivid::effects {

Particles::Particles() {
    m_rng.seed(m_seed);
    m_particles.reserve(m_maxParticles);
}

Particles::~Particles() {
    cleanup();
}

void Particles::init(Context& ctx) {
    if (m_initialized) return;

    createOutput(ctx);
    m_renderer.init(ctx.device(), ctx.queue());

    // Load texture if specified
    if (!m_texturePath.empty()) {
        loadTexture(ctx);
    }

    m_initialized = true;
}

void Particles::loadTexture(Context& ctx) {
    // TODO: Implement texture loading using stb_image or similar
    // For now, sprite mode will fall back to circles
    // This would load m_texturePath and create m_spriteTexture + m_spriteTextureView
}

void Particles::process(Context& ctx) {
    if (!m_initialized) init(ctx);

    float dt = static_cast<float>(ctx.dt());

    // Handle burst emission
    if (m_needsBurst) {
        for (int i = 0; i < m_burstCount && static_cast<int>(m_particles.size()) < m_maxParticles; i++) {
            emitParticle(m_emitterPos);
        }
        m_needsBurst = false;
    }

    // Continuous emission
    m_emitAccumulator += m_emitRate * dt;
    while (m_emitAccumulator >= 1.0f && static_cast<int>(m_particles.size()) < m_maxParticles) {
        emitParticle(m_emitterPos);
        m_emitAccumulator -= 1.0f;
    }

    // Update particles
    updateParticles(dt);

    // Remove dead particles
    m_particles.erase(
        std::remove_if(m_particles.begin(), m_particles.end(),
            [](const Particle& p) { return p.life <= 0.0f; }),
        m_particles.end()
    );

    // Build render data
    if (m_useSprites && m_spriteTextureView) {
        // Render as textured sprites
        std::vector<Sprite2D> sprites;
        sprites.reserve(m_particles.size());

        for (const auto& p : m_particles) {
            float lifeRatio = p.life / p.maxLife;
            float age = 1.0f - lifeRatio;

            // Calculate size
            float size = glm::mix(m_sizeStart, m_sizeEnd, age);
            size *= p.size / m_sizeStart;  // Apply per-particle variation

            // Calculate color
            glm::vec4 color = getParticleColor(p, age);

            // Apply fade in/out
            float alpha = color.a;
            if (m_fadeInTime > 0.0f && age < m_fadeInTime) {
                alpha *= age / m_fadeInTime;
            }
            if (m_fadeOut) {
                alpha *= lifeRatio;
            }
            color.a = alpha;

            Sprite2D sprite;
            sprite.position = p.position;
            sprite.size = size;
            sprite.rotation = p.rotation;
            sprite.color = color;
            sprite.uvOffset = glm::vec2(0.0f);
            sprite.uvScale = glm::vec2(1.0f);
            sprites.push_back(sprite);
        }

        m_renderer.renderSprites(ctx, sprites, m_spriteTextureView, m_outputView,
                                 m_width, m_height, m_clearColor);
    } else {
        // Render as SDF circles
        std::vector<Circle2D> circles;
        circles.reserve(m_particles.size());

        for (const auto& p : m_particles) {
            float lifeRatio = p.life / p.maxLife;
            float age = 1.0f - lifeRatio;

            // Calculate size
            float size = glm::mix(m_sizeStart, m_sizeEnd, age);
            size *= p.size / m_sizeStart;  // Apply per-particle variation

            // Calculate color
            glm::vec4 color = getParticleColor(p, age);

            // Apply fade in/out
            float alpha = color.a;
            if (m_fadeInTime > 0.0f && age < m_fadeInTime) {
                alpha *= age / m_fadeInTime;
            }
            if (m_fadeOut) {
                alpha *= lifeRatio;
            }
            color.a = alpha;

            circles.emplace_back(p.position, size, color);
        }

        m_renderer.renderCircles(ctx, circles, m_outputView, m_width, m_height, m_clearColor);
    }
}

void Particles::emitParticle(const glm::vec2& emitterPos) {
    Particle p;
    p.index = m_particleIndex++;

    // Initial position based on emitter shape
    p.position = getEmitterPosition(emitterPos);

    // Initial velocity
    p.velocity = getInitialVelocity(p.position, emitterPos);

    // Lifetime with variation
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    p.maxLife = m_baseLife * (1.0f + m_lifeVariation * dist(m_rng));
    p.life = p.maxLife;

    // Size with variation
    p.size = m_sizeStart * (1.0f + m_sizeVariation * dist(m_rng));

    // Rotation for sprites
    std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * 3.14159265f);
    p.rotation = angleDist(m_rng);
    p.angularVel = m_spinSpeed * (0.5f + 0.5f * dist(m_rng));

    // Initial color
    p.color = m_colorStart;

    m_particles.push_back(p);
}

glm::vec2 Particles::getEmitterPosition(const glm::vec2& center) {
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);

    switch (m_emitterShape) {
        case EmitterShape::Point:
            return center;

        case EmitterShape::Line: {
            float offset = dist(m_rng) * m_emitterSize * 0.5f;
            float ca = std::cos(m_emitterAngle);
            float sa = std::sin(m_emitterAngle);
            return center + glm::vec2(offset * ca, offset * sa);
        }

        case EmitterShape::Ring: {
            float angle = dist01(m_rng) * 2.0f * 3.14159265f;
            return center + m_emitterSize * glm::vec2(std::cos(angle), std::sin(angle));
        }

        case EmitterShape::Disc: {
            float angle = dist01(m_rng) * 2.0f * 3.14159265f;
            float radius = std::sqrt(dist01(m_rng)) * m_emitterSize;
            return center + radius * glm::vec2(std::cos(angle), std::sin(angle));
        }

        case EmitterShape::Rectangle:
            return center + glm::vec2(dist(m_rng), dist(m_rng)) * m_emitterSize * 0.5f;

        default:
            return center;
    }
}

glm::vec2 Particles::getInitialVelocity(const glm::vec2& pos, const glm::vec2& emitterCenter) {
    glm::vec2 vel = m_baseVelocity;

    // Add radial velocity (away from center)
    if (m_radialVelocity != 0.0f) {
        glm::vec2 dir = pos - emitterCenter;
        if (glm::length(dir) > 0.001f) {
            vel += glm::normalize(dir) * m_radialVelocity;
        } else {
            // Random direction if at center
            std::uniform_real_distribution<float> dist(0.0f, 2.0f * 3.14159265f);
            float angle = dist(m_rng);
            vel += m_radialVelocity * glm::vec2(std::cos(angle), std::sin(angle));
        }
    }

    // Apply spread (cone of randomness)
    if (m_spread > 0.0f) {
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        float angle = dist(m_rng) * m_spread * 0.5f;
        float ca = std::cos(angle);
        float sa = std::sin(angle);
        vel = glm::vec2(vel.x * ca - vel.y * sa, vel.x * sa + vel.y * ca);
    }

    // Velocity variation
    if (m_velocityVariation > 0.0f) {
        std::uniform_real_distribution<float> dist(1.0f - m_velocityVariation, 1.0f + m_velocityVariation);
        vel *= dist(m_rng);
    }

    return vel;
}

void Particles::updateParticles(float dt) {
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    for (auto& p : m_particles) {
        // Apply gravity
        p.velocity.y += m_gravity * dt;

        // Apply drag
        if (m_drag > 0.0f) {
            p.velocity *= (1.0f - m_drag * dt);
        }

        // Apply turbulence
        if (m_turbulence > 0.0f) {
            p.velocity += glm::vec2(dist(m_rng), dist(m_rng)) * m_turbulence * dt;
        }

        // Apply attractor
        if (m_attractorStrength != 0.0f) {
            glm::vec2 toAttractor = m_attractorPos - p.position;
            float distance = glm::length(toAttractor);
            if (distance > 0.01f) {
                p.velocity += glm::normalize(toAttractor) * m_attractorStrength * dt / distance;
            }
        }

        // Update position
        p.position += p.velocity * dt;

        // Update rotation (for sprites)
        p.rotation += p.angularVel * dt;

        // Update life
        p.life -= dt;
    }
}

glm::vec4 Particles::getParticleColor(const Particle& p, float age) {
    switch (m_colorMode) {
        case ColorMode::Solid:
            return m_colorStart;

        case ColorMode::Gradient:
            return glm::mix(m_colorStart, m_colorEnd, age);

        case ColorMode::Rainbow: {
            float hue = std::fmod(p.index * 0.1f, 1.0f);
            return hsvToRgb(hue, 0.8f, 1.0f);
        }

        case ColorMode::Random: {
            // Use particle index as seed for consistent random color
            std::mt19937 localRng(p.index);
            std::uniform_real_distribution<float> dist(0.0f, 1.0f);
            return glm::vec4(dist(localRng), dist(localRng), dist(localRng), 1.0f);
        }

        default:
            return m_colorStart;
    }
}

glm::vec4 Particles::hsvToRgb(float h, float s, float v) {
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

void Particles::cleanup() {
    m_renderer.cleanup();

    if (m_spriteTexture) {
        wgpuTextureRelease(m_spriteTexture);
        m_spriteTexture = nullptr;
    }
    if (m_spriteTextureView) {
        wgpuTextureViewRelease(m_spriteTextureView);
        m_spriteTextureView = nullptr;
    }

    releaseOutput();
    m_initialized = false;
    m_particles.clear();
}

} // namespace vivid::effects
