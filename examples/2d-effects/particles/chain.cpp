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
    fire.emitter(EmitterShape::Point)
        .position(0.5f, 0.85f)
        .emitRate(100.0f)
        .velocity(0.0f, -0.15f)
        .spread(25.0f)
        .gravity(-0.05f)
        .life(1.5f)
        .lifeVariation(0.3f)
        .size(0.025f, 0.005f)
        .color(1.0f, 0.84f, 0.0f, 1.0f)  // Gold
        .colorEnd(1.0f, 0.27f, 0.0f, 0.0f)  // OrangeRed, faded
        .fadeOut(true)
        .clearColor(0.02f, 0.02f, 0.06f, 1.0f);  // Dark blue

    // Fountain particles - arcing water effect
    auto& fountain = chain.add<Particles>("fountain");
    fountain.emitter(EmitterShape::Point)
        .position(0.5f, 0.7f)
        .emitRate(80.0f)
        .velocity(0.0f, -0.25f)
        .spread(15.0f)
        .gravity(0.12f)
        .life(2.0f)
        .size(0.012f, 0.008f)
        .color(0.12f, 0.56f, 1.0f, 1.0f)  // DodgerBlue
        .colorEnd(0.0f, 0.0f, 0.8f, 0.0f)  // MediumBlue, faded
        .fadeOut(true)
        .clearColor(0.0f, 0.0f, 0.0f, 0.0f);  // Transparent

    // Ring particles - expanding ring
    auto& ring = chain.add<Particles>("ring");
    ring.emitter(EmitterShape::Ring)
        .position(0.5f, 0.5f)
        .emitterSize(0.1f)
        .emitRate(60.0f)
        .radialVelocity(0.15f)
        .gravity(0.0f)
        .drag(1.5f)
        .life(1.2f)
        .size(0.018f, 0.0f)
        .colorMode(ColorMode::Rainbow)
        .fadeOut(true)
        .clearColor(0.0f, 0.0f, 0.0f, 0.0f);

    // Composite all particle layers
    auto& comp = chain.add<Composite>("comp");
    comp.input(0, &fire)
        .input(1, &fountain)
        .input(2, &ring)
        .mode(BlendMode::Add);

    chain.output("comp");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = static_cast<float>(ctx.time());

    // Fire follows mouse position
    glm::vec2 mouse = ctx.mouseNorm();
    float fireX = mouse.x * 0.5f + 0.5f;
    float fireY = mouse.y * -0.5f + 0.5f;
    chain.get<Particles>("fire").position(fireX, fireY);

    // Pulsing emit rate for fountain
    float rate = 60.0f + 30.0f * std::sin(time * 2.0f);
    chain.get<Particles>("fountain").emitRate(rate);

    // Rotating ring emitter
    float ringX = 0.5f + 0.12f * std::cos(time * 0.8f);
    float ringY = 0.5f + 0.12f * std::sin(time * 0.8f);
    chain.get<Particles>("ring").position(ringX, ringY);
}

VIVID_CHAIN(setup, update)
