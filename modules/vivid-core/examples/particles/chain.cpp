// Particles Demo - Vivid Example
// Demonstrates 2D particle system with physics

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Fire particles - rising flame effect
    auto& fire = chain.add<Particles>("fire");
    fire.emitterShape = EmitterShape::Point;
    fire.position.set(0.5f, 0.85f);
    fire.emitRate = 100.0f;
    fire.velocity.set(0.0f, -0.15f);
    fire.spread = 25.0f;
    fire.gravity = -0.05f;
    fire.life = 1.5f;
    fire.lifeVariation = 0.3f;
    fire.size = 0.025f;
    fire.sizeEnd = 0.005f;
    fire.color.set(1.0f, 0.84f, 0.0f, 1.0f);  // Gold
    fire.colorEnd.set(1.0f, 0.27f, 0.0f, 0.0f);  // OrangeRed, faded
    fire.fadeOut = true;
    fire.clearColor.set(0.02f, 0.02f, 0.06f, 1.0f);  // Dark blue

    // Fountain particles - arcing water effect
    auto& fountain = chain.add<Particles>("fountain");
    fountain.emitterShape = EmitterShape::Point;
    fountain.position.set(0.5f, 0.7f);
    fountain.emitRate = 80.0f;
    fountain.velocity.set(0.0f, -0.25f);
    fountain.spread = 15.0f;
    fountain.gravity = 0.12f;
    fountain.life = 2.0f;
    fountain.size = 0.012f;
    fountain.sizeEnd = 0.008f;
    fountain.color.set(0.12f, 0.56f, 1.0f, 1.0f);  // DodgerBlue
    fountain.colorEnd.set(0.0f, 0.0f, 0.8f, 0.0f);  // MediumBlue, faded
    fountain.fadeOut = true;
    fountain.clearColor.set(0.0f, 0.0f, 0.0f, 0.0f);  // Transparent

    // Ring particles - expanding ring
    auto& ring = chain.add<Particles>("ring");
    ring.emitterShape = EmitterShape::Ring;
    ring.position.set(0.5f, 0.5f);
    ring.emitterSize = 0.1f;
    ring.emitRate = 60.0f;
    ring.radialVelocity = 0.15f;
    ring.gravity = 0.0f;
    ring.drag = 1.5f;
    ring.life = 1.2f;
    ring.size = 0.018f;
    ring.sizeEnd = 0.0f;
    ring.colorMode = ColorMode::Rainbow;
    ring.fadeOut = true;
    ring.clearColor.set(0.0f, 0.0f, 0.0f, 0.0f);

    // Composite all particle layers
    auto& comp = chain.add<Composite>("comp");
    comp.input(0, "fire");
    comp.input(1, "fountain");
    comp.input(2, "ring");
    comp.mode = BlendMode::Add;

    chain.output("comp");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = static_cast<float>(ctx.time());

    // Fire follows mouse position - mouseNorm() returns 0-1 with Y-down
    glm::vec2 mouse = ctx.mouseNorm();
    float fireX = mouse.x;
    float fireY = mouse.y;  // Y-down matches particle coordinates
    chain.get<Particles>("fire").position.set(fireX, fireY);

    // Pulsing emit rate for fountain
    float rate = 60.0f + 30.0f * std::sin(time * 2.0f);
    chain.get<Particles>("fountain").emitRate = rate;

    // Rotating ring emitter
    float ringX = 0.5f + 0.12f * std::cos(time * 0.8f);
    float ringY = 0.5f + 0.12f * std::sin(time * 0.8f);
    chain.get<Particles>("ring").position.set(ringX, ringY);

    // Debug value monitoring - visible in the debug panel (D key)
    ctx.debug("fire.x", fireX);
    ctx.debug("fire.y", fireY);
    ctx.debug("fountain.rate", rate);
    ctx.debug("ring.x", ringX);
    ctx.debug("ring.y", ringY);
}

VIVID_CHAIN(setup, update)
