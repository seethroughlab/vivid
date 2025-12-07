// Particles Demo - Vivid Example
// Demonstrates 2D particle system with physics

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

// Chain (persistent across hot-reloads)
static Chain* chain = nullptr;

void setup(Context& ctx) {
    // Clean up previous chain if hot-reloading
    delete chain;
    chain = nullptr;

    // Create chain
    chain = new Chain();

    // Fire particles - rising flame effect
    auto& fire = chain->add<Particles>("fire");
    fire.emitter(EmitterShape::Point)
        .position(0.5f, 0.85f)
        .emitRate(100.0f)
        .velocity(0.0f, -0.15f)
        .spread(25.0f)
        .gravity(-0.05f)  // Negative = rises
        .life(1.5f)
        .lifeVariation(0.3f)
        .size(0.025f, 0.005f)
        .color(1.0f, 0.8f, 0.2f, 1.0f)
        .colorEnd(1.0f, 0.2f, 0.1f, 0.0f)
        .fadeOut(true)
        .clearColor(0.02f, 0.02f, 0.05f, 1.0f);

    // Fountain particles - arcing water effect
    auto& fountain = chain->add<Particles>("fountain");
    fountain.emitter(EmitterShape::Point)
        .position(0.5f, 0.7f)
        .emitRate(80.0f)
        .velocity(0.0f, -0.25f)
        .spread(15.0f)
        .gravity(0.12f)  // Falls down
        .life(2.0f)
        .size(0.012f, 0.008f)
        .color(0.3f, 0.6f, 1.0f, 1.0f)
        .colorEnd(0.1f, 0.3f, 0.8f, 0.0f)
        .fadeOut(true)
        .clearColor(0.0f, 0.0f, 0.0f, 0.0f);

    // Ring particles - expanding ring
    auto& ring = chain->add<Particles>("ring");
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

    // Composite: fire + fountain
    auto& comp1 = chain->add<Composite>("comp1");
    comp1.inputA(&fire);
    comp1.inputB(&fountain);
    comp1.mode(BlendMode::Add);

    // Composite: (fire + fountain) + ring
    auto& comp2 = chain->add<Composite>("comp2");
    comp2.inputA(&comp1);
    comp2.inputB(&ring);
    comp2.mode(BlendMode::Add);

    // Output
    chain->add<Output>("output").input(&comp2);
    chain->setOutput("output");
    chain->init(ctx);

    if (chain->hasError()) {
        ctx.setError(chain->error());
    }
}

void update(Context& ctx) {
    if (!chain) return;

    float time = static_cast<float>(ctx.time());

    // Fire follows mouse position
    glm::vec2 mouse = ctx.mouseNorm();
    float fireX = mouse.x * 0.5f + 0.5f;  // Convert from [-1,1] to [0,1]
    float fireY = mouse.y * -0.5f + 0.5f; // Flip Y and convert
    chain->get<Particles>("fire").position(fireX, fireY);

    // Pulsing emit rate for fountain
    float rate = 60.0f + 30.0f * std::sin(time * 2.0f);
    chain->get<Particles>("fountain").emitRate(rate);

    // Rotating ring emitter
    float ringX = 0.5f + 0.12f * std::cos(time * 0.8f);
    float ringY = 0.5f + 0.12f * std::sin(time * 0.8f);
    chain->get<Particles>("ring").position(ringX, ringY);

    // Process chain (Output handles ctx.setOutputTexture internally)
    chain->process(ctx);
}

VIVID_CHAIN(setup, update)
