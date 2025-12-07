// Particles Demo - Vivid Example
// Demonstrates 2D particle system with physics

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

// Operators (persistent across hot-reloads)
static Particles* fire = nullptr;
static Particles* fountain = nullptr;
static Particles* ring = nullptr;
static Composite* comp1 = nullptr;
static Composite* comp2 = nullptr;
static Output* output = nullptr;

void setup(Context& ctx) {
    // Clean up previous operators if hot-reloading
    delete fire;
    delete fountain;
    delete ring;
    delete comp1;
    delete comp2;
    delete output;
    fire = nullptr;
    fountain = nullptr;
    ring = nullptr;
    comp1 = nullptr;
    comp2 = nullptr;
    output = nullptr;

    // Create operators
    fire = new Particles();
    fountain = new Particles();
    ring = new Particles();
    comp1 = new Composite();
    comp2 = new Composite();
    output = new Output();

    // Fire particles - rising flame effect
    fire->emitter(EmitterShape::Point)
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
    fountain->emitter(EmitterShape::Point)
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
    ring->emitter(EmitterShape::Ring)
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
    comp1->inputA(fire);
    comp1->inputB(fountain);
    comp1->mode(BlendMode::Add);

    // Composite: (fire + fountain) + ring
    comp2->inputA(comp1);
    comp2->inputB(ring);
    comp2->mode(BlendMode::Add);

    // Output
    output->input(comp2);

    // Register operators for visualization (press Tab to toggle)
    ctx.registerOperator("fire", fire);
    ctx.registerOperator("fountain", fountain);
    ctx.registerOperator("ring", ring);
    ctx.registerOperator("comp1", comp1);
    ctx.registerOperator("comp2", comp2);
    ctx.registerOperator("output", output);
}

void update(Context& ctx) {
    float time = static_cast<float>(ctx.time());

    // Animate fire position side to side
    float fireX = 0.5f + 0.15f * std::sin(time * 0.5f);
    fire->position(fireX, 0.85f);

    // Pulsing emit rate for fountain
    float rate = 60.0f + 30.0f * std::sin(time * 2.0f);
    fountain->emitRate(rate);

    // Rotating ring emitter
    float ringX = 0.5f + 0.12f * std::cos(time * 0.8f);
    float ringY = 0.5f + 0.12f * std::sin(time * 0.8f);
    ring->position(ringX, ringY);

    // Process the chain
    fire->process(ctx);
    fountain->process(ctx);
    ring->process(ctx);
    comp1->process(ctx);
    comp2->process(ctx);
    output->process(ctx);
}

VIVID_CHAIN(setup, update)
