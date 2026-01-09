// Cosmic Nebula - Vivid Example
// Demonstrates 2D particle system with layered composition and post-processing

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Layer 1: Dense swirling particles with attractor
    auto& swirl = chain.add<Particles>("swirl");
    swirl.setTexture("assets/glow.png");  // Soft gaussian sprite
    swirl.emitterShape = EmitterShape::Disc;  // Fill area, not just ring
    swirl.position.set(0.5f, 0.5f);
    swirl.emitterSize = 0.25f;         // Size in Y, will scale X by aspect
    swirl.emitRate = 600.0f;           // Higher density
    swirl.maxParticles = 15000;
    swirl.radialVelocity = 0.02f;      // Very slow drift
    swirl.spread = 360.0f;             // All directions
    swirl.turbulence = 1.2f;           // More organic movement
    swirl.attractorPosition.set(0.5f, 0.5f);
    swirl.attractorStrength = 0.08f;   // Gentler pull
    swirl.drag = 0.4f;
    swirl.gravity = 0.0f;
    swirl.size = 0.004f;               // Much smaller
    swirl.sizeEnd = 0.001f;
    swirl.life = 5.0f;                 // Longer life for buildup
    swirl.lifeVariation = 0.5f;
    swirl.colorMode = ColorMode::Rainbow;
    swirl.fadeOut = true;
    swirl.clearColor.set(0.0f, 0.0f, 0.02f, 1.0f);

    // Layer 2: Bright core glow
    auto& core = chain.add<Particles>("core");
    core.setTexture("assets/glow.png");  // Soft gaussian sprite
    core.emitterShape = EmitterShape::Disc;
    core.position.set(0.5f, 0.5f);
    core.emitterSize = 0.04f;          // Smaller core
    core.emitRate = 200.0f;
    core.maxParticles = 3000;
    core.radialVelocity = 0.06f;
    core.spread = 360.0f;
    core.turbulence = 0.5f;
    core.drag = 0.6f;
    core.size = 0.010f;
    core.sizeEnd = 0.002f;
    core.life = 2.0f;
    core.color.set(1.0f, 1.0f, 1.0f, 1.0f);
    core.colorEnd.set(0.6f, 0.85f, 1.0f, 0.0f);
    core.colorMode = ColorMode::Gradient;
    core.fadeOut = true;
    core.clearColor.set(0.0f, 0.0f, 0.0f, 0.0f);

    // Composite layers with additive blending
    auto& comp = chain.add<Composite>("comp");
    comp.input(0, "swirl");
    comp.input(1, "core");
    comp.mode = BlendMode::Add;

    // Bloom for ethereal glow
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("comp");
    bloom.threshold = 0.15f;
    bloom.intensity = 1.2f;
    bloom.radius = 25.0f;

    chain.output("bloom");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    auto& swirl = chain.get<Particles>("swirl");
    auto& core = chain.get<Particles>("core");

    // Animate turbulence
    swirl.turbulence = 0.6f + 0.3f * std::sin(t * 0.4f);

    // Mouse controls attractor (Y) and emitter size (X)
    glm::vec2 mouse = ctx.mouseNorm();
    swirl.attractorStrength = -0.2f + mouse.y * 0.5f;
    swirl.emitterSize = 0.2f + mouse.x * 0.3f;

    // Breathing core
    float breathe = 0.08f + 0.03f * std::sin(t * 1.5f);
    core.emitterSize = breathe;
}

VIVID_CHAIN(setup, update)
