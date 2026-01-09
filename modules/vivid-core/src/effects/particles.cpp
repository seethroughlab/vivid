// Vivid Effects 2D - Particles Operator Implementation
// 2D GPU particle system with emitters, physics, and lifetime

#include <vivid/effects/particles.h>
#include <vivid/context.h>
#include <vivid/io/image_loader.h>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <iostream>

namespace vivid::effects {

Particles::Particles() {
    m_rng.seed(m_seed);
    m_particles.reserve(static_cast<int>(maxParticles));
}

Particles::~Particles() {
    cleanup();
}

void Particles::init(Context& ctx) {
    if (!beginInit()) return;

    createOutput(ctx);
    m_renderer.init(ctx.device(), ctx.queue());

    // Load texture if specified
    if (!m_texturePath.empty()) {
        loadTexture(ctx);
    }
}

void Particles::loadTexture(Context& ctx) {
    if (m_texturePath.empty()) return;

    // Load image via vivid-io
    auto imageData = vivid::io::loadImage(m_texturePath);

    if (!imageData.valid()) {
        std::cerr << "[Particles] Failed to load sprite: " << m_texturePath << std::endl;
        m_useSprites = false;  // Fall back to circles
        return;
    }

    int width = imageData.width;
    int height = imageData.height;

    // Release old texture if exists
    if (m_spriteTexture) {
        wgpuTextureRelease(m_spriteTexture);
        m_spriteTexture = nullptr;
    }
    if (m_spriteTextureView) {
        wgpuTextureViewRelease(m_spriteTextureView);
        m_spriteTextureView = nullptr;
    }

    // Create GPU texture using EFFECTS_FORMAT for compatibility
    WGPUTextureDescriptor texDesc = {};
    texDesc.label = toStringView("Particle Sprite");
    texDesc.size.width = width;
    texDesc.size.height = height;
    texDesc.size.depthOrArrayLayers = 1;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = EFFECTS_FORMAT;  // RGBA16Float
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;

    m_spriteTexture = wgpuDeviceCreateTexture(ctx.device(), &texDesc);
    if (!m_spriteTexture) {
        std::cerr << "[Particles] Failed to create sprite texture" << std::endl;
        m_useSprites = false;
        return;
    }

    // Create texture view
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = EFFECTS_FORMAT;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;
    m_spriteTextureView = wgpuTextureCreateView(m_spriteTexture, &viewDesc);

    // Convert 8-bit RGBA to 16-bit float RGBA
    std::vector<uint16_t> floatPixels(width * height * 4);
    for (size_t i = 0; i < imageData.pixels.size(); ++i) {
        float normalized = imageData.pixels[i] / 255.0f;

        // Handle zero specially
        if (normalized == 0.0f) {
            floatPixels[i] = 0;
            continue;
        }

        // Convert float32 to float16 using bit manipulation
        uint32_t f32;
        std::memcpy(&f32, &normalized, sizeof(float));
        uint32_t sign = (f32 >> 16) & 0x8000;
        int32_t exp = static_cast<int32_t>(((f32 >> 23) & 0xFF)) - 127 + 15;
        uint32_t mant = (f32 >> 13) & 0x3FF;

        if (exp <= 0) {
            floatPixels[i] = static_cast<uint16_t>(sign);
        } else if (exp >= 31) {
            floatPixels[i] = static_cast<uint16_t>(sign | 0x7C00);
        } else {
            floatPixels[i] = static_cast<uint16_t>(sign | (exp << 10) | mant);
        }
    }

    // Upload pixel data
    WGPUTexelCopyTextureInfo destination = {};
    destination.texture = m_spriteTexture;
    destination.mipLevel = 0;
    destination.origin = {0, 0, 0};
    destination.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferLayout dataLayout = {};
    dataLayout.offset = 0;
    dataLayout.bytesPerRow = width * 4 * sizeof(uint16_t);
    dataLayout.rowsPerImage = height;

    WGPUExtent3D writeSize = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};

    wgpuQueueWriteTexture(ctx.queue(), &destination, floatPixels.data(),
                          floatPixels.size() * sizeof(uint16_t), &dataLayout, &writeSize);

    std::cout << "[Particles] Loaded sprite: " << m_texturePath
              << " (" << width << "x" << height << ")" << std::endl;
}

void Particles::process(Context& ctx) {
    if (!isInitialized()) init(ctx);
    // Generators use their declared resolution (default 1280x720)

    // Particles is a simulation - always cooks

    // Update aspect ratio for circular emitter shapes
    m_aspectRatio = static_cast<float>(m_width) / m_height;

    float dt = static_cast<float>(ctx.dt());

    // Get current param values
    glm::vec2 emitterPos(position.x(), position.y());
    float emitRateVal = static_cast<float>(emitRate);
    int maxParticlesVal = static_cast<int>(maxParticles);

    // Handle burst emission
    if (m_needsBurst) {
        for (int i = 0; i < m_burstCount && static_cast<int>(m_particles.size()) < maxParticlesVal; i++) {
            emitParticle(emitterPos);
        }
        m_needsBurst = false;
    }

    // Continuous emission
    m_emitAccumulator += emitRateVal * dt;
    while (m_emitAccumulator >= 1.0f && static_cast<int>(m_particles.size()) < maxParticlesVal) {
        emitParticle(emitterPos);
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

    // Get rendering params
    float sizeStartVal = static_cast<float>(size);
    float sizeEndVal = static_cast<float>(sizeEnd);
    float fadeInTimeVal = static_cast<float>(fadeInTime);
    bool fadeOutVal = static_cast<bool>(fadeOut);
    glm::vec4 clearColorVal(clearColor.r(), clearColor.g(), clearColor.b(), clearColor.a());

    // Build render data
    if (m_useSprites && m_spriteTextureView) {
        // Render as textured sprites
        std::vector<Sprite2D> sprites;
        sprites.reserve(m_particles.size());

        for (const auto& p : m_particles) {
            float lifeRatio = p.life / p.maxLife;
            float age = 1.0f - lifeRatio;

            // Calculate size
            float sz = glm::mix(sizeStartVal, sizeEndVal, age);
            sz *= p.size / sizeStartVal;  // Apply per-particle variation

            // Calculate color
            glm::vec4 col = getParticleColor(p, age);

            // Apply fade in/out
            float alpha = col.a;
            if (fadeInTimeVal > 0.0f && age < fadeInTimeVal) {
                alpha *= age / fadeInTimeVal;
            }
            if (fadeOutVal) {
                alpha *= lifeRatio;
            }
            col.a = alpha;

            Sprite2D sprite;
            sprite.position = p.position;
            sprite.size = sz;
            sprite.rotation = p.rotation;
            sprite.color = col;
            sprite.uvOffset = glm::vec2(0.0f);
            sprite.uvScale = glm::vec2(1.0f);
            sprites.push_back(sprite);
        }

        m_renderer.renderSprites(ctx, sprites, m_spriteTextureView, m_outputView,
                                 m_width, m_height, clearColorVal);
    } else {
        // Render as SDF circles
        std::vector<Circle2D> circles;
        circles.reserve(m_particles.size());

        for (const auto& p : m_particles) {
            float lifeRatio = p.life / p.maxLife;
            float age = 1.0f - lifeRatio;

            // Calculate size
            float sz = glm::mix(sizeStartVal, sizeEndVal, age);
            sz *= p.size / sizeStartVal;  // Apply per-particle variation

            // Calculate color
            glm::vec4 col = getParticleColor(p, age);

            // Apply fade in/out
            float alpha = col.a;
            if (fadeInTimeVal > 0.0f && age < fadeInTimeVal) {
                alpha *= age / fadeInTimeVal;
            }
            if (fadeOutVal) {
                alpha *= lifeRatio;
            }
            col.a = alpha;

            circles.emplace_back(p.position, sz, col);
        }

        m_renderer.renderCircles(ctx, circles, m_outputView, m_width, m_height, clearColorVal);
    }

    didCook();
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
    float lifeVal = static_cast<float>(life);
    float lifeVarVal = static_cast<float>(lifeVariation);
    p.maxLife = lifeVal * (1.0f + lifeVarVal * dist(m_rng));
    p.life = p.maxLife;

    // Size with variation
    float sizeVal = static_cast<float>(size);
    float sizeVarVal = static_cast<float>(sizeVariation);
    p.size = sizeVal * (1.0f + sizeVarVal * dist(m_rng));

    // Rotation for sprites
    std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * 3.14159265f);
    p.rotation = angleDist(m_rng);
    p.angularVel = m_spinSpeed * (0.5f + 0.5f * dist(m_rng));

    // Initial color
    p.color = glm::vec4(color.r(), color.g(), color.b(), color.a());

    m_particles.push_back(p);
}

glm::vec2 Particles::getEmitterPosition(const glm::vec2& center) {
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);

    float emitterSizeVal = static_cast<float>(emitterSize);
    float emitterAngleVal = static_cast<float>(emitterAngle);

    switch (emitterShape) {
        case EmitterShape::Point:
            return center;

        case EmitterShape::Line: {
            float offset = dist(m_rng) * emitterSizeVal * 0.5f;
            float ca = std::cos(emitterAngleVal);
            float sa = std::sin(emitterAngleVal);
            return center + glm::vec2(offset * ca, offset * sa);
        }

        case EmitterShape::Ring: {
            float angle = dist01(m_rng) * 2.0f * 3.14159265f;
            // Scale X by 1/aspect to make circle appear round on widescreen
            return center + emitterSizeVal * glm::vec2(std::cos(angle) / m_aspectRatio, std::sin(angle));
        }

        case EmitterShape::Disc: {
            float angle = dist01(m_rng) * 2.0f * 3.14159265f;
            float radius = std::sqrt(dist01(m_rng)) * emitterSizeVal;
            // Scale X by 1/aspect to make circle appear round on widescreen
            return center + radius * glm::vec2(std::cos(angle) / m_aspectRatio, std::sin(angle));
        }

        case EmitterShape::Rectangle:
            return center + glm::vec2(dist(m_rng), dist(m_rng)) * emitterSizeVal * 0.5f;

        default:
            return center;
    }
}

glm::vec2 Particles::getInitialVelocity(const glm::vec2& pos, const glm::vec2& emitterCenter) {
    glm::vec2 vel(velocity.x(), velocity.y());
    float radialVelVal = static_cast<float>(radialVelocity);
    float spreadVal = glm::radians(static_cast<float>(spread));
    float velVarVal = static_cast<float>(velocityVariation);

    // Add radial velocity (away from center)
    if (radialVelVal != 0.0f) {
        glm::vec2 dir = pos - emitterCenter;
        if (glm::length(dir) > 0.001f) {
            vel += glm::normalize(dir) * radialVelVal;
        } else {
            // Random direction if at center
            std::uniform_real_distribution<float> dist(0.0f, 2.0f * 3.14159265f);
            float angle = dist(m_rng);
            vel += radialVelVal * glm::vec2(std::cos(angle), std::sin(angle));
        }
    }

    // Apply spread (cone of randomness)
    if (spreadVal > 0.0f) {
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        float angle = dist(m_rng) * spreadVal * 0.5f;
        float ca = std::cos(angle);
        float sa = std::sin(angle);
        vel = glm::vec2(vel.x * ca - vel.y * sa, vel.x * sa + vel.y * ca);
    }

    // Velocity variation
    if (velVarVal > 0.0f) {
        std::uniform_real_distribution<float> dist(1.0f - velVarVal, 1.0f + velVarVal);
        vel *= dist(m_rng);
    }

    return vel;
}

void Particles::updateParticles(float dt) {
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    float gravityVal = static_cast<float>(gravity);
    float dragVal = static_cast<float>(drag);
    float turbulenceVal = static_cast<float>(turbulence);
    glm::vec2 attractorPosVal(attractorPosition.x(), attractorPosition.y());
    float attractorStrengthVal = static_cast<float>(attractorStrength);

    for (auto& p : m_particles) {
        // Apply gravity
        p.velocity.y += gravityVal * dt;

        // Apply drag
        if (dragVal > 0.0f) {
            p.velocity *= (1.0f - dragVal * dt);
        }

        // Apply turbulence
        if (turbulenceVal > 0.0f) {
            p.velocity += glm::vec2(dist(m_rng), dist(m_rng)) * turbulenceVal * dt;
        }

        // Apply attractor
        if (attractorStrengthVal != 0.0f) {
            glm::vec2 toAttractor = attractorPosVal - p.position;
            float distance = glm::length(toAttractor);
            if (distance > 0.01f) {
                p.velocity += glm::normalize(toAttractor) * attractorStrengthVal * dt / distance;
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
    glm::vec4 colorStartVal(color.r(), color.g(), color.b(), color.a());
    glm::vec4 colorEndVal(colorEnd.r(), colorEnd.g(), colorEnd.b(), colorEnd.a());

    switch (colorMode) {
        case ColorMode::Solid:
            return colorStartVal;

        case ColorMode::Gradient:
            return glm::mix(colorStartVal, colorEndVal, age);

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
            return colorStartVal;
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
