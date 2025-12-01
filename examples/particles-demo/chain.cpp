// Particles Demo - 2D particle system with physics
#include <vivid/vivid.h>
#include <cmath>

using namespace vivid;

void setup(Chain& chain) {
    // Fire-like particles from a point
    chain.add<Particles>("fire")
        .emitterShape(Particles::Point)
        .emitPosition(0.5f, 0.9f)
        .emitRate(100.0f)
        .life(1.5f)
        .lifeVariation(0.3f)
        .speed(0.2f)
        .speedVariation(0.1f)
        .angle(-90.0f)  // Upward
        .spread(30.0f)
        .gravity(-0.1f)  // Negative = rise
        .startSize(0.03f)
        .endSize(0.005f)
        .startColor(1.0f, 0.8f, 0.2f, 1.0f)
        .endColor(1.0f, 0.2f, 0.1f, 0.0f)
        .clearColor(0.02f, 0.02f, 0.05f);

    // Fountain particles from center
    chain.add<Particles>("fountain")
        .emitterShape(Particles::Point)
        .emitPosition(0.5f, 0.7f)
        .emitRate(80.0f)
        .life(2.0f)
        .speed(0.3f)
        .speedVariation(0.05f)
        .angle(-90.0f)
        .spread(20.0f)
        .gravity(0.15f)  // Falls down
        .startSize(0.015f)
        .endSize(0.01f)
        .startColor(0.3f, 0.6f, 1.0f, 1.0f)
        .endColor(0.1f, 0.3f, 0.8f, 0.0f)
        .clearColor(0.02f, 0.02f, 0.05f);

    // Ring emitter with outward spray
    chain.add<Particles>("ring")
        .emitterShape(Particles::Ring)
        .emitPosition(0.5f, 0.5f)
        .emitterRadius(0.15f)
        .emitRate(60.0f)
        .life(1.2f)
        .speed(0.15f)
        .emitFromEdge(true)  // Spray outward from ring
        .gravity(0.0f)
        .drag(1.0f)
        .startSize(0.02f)
        .endSize(0.0f)
        .startColor(0.2f, 1.0f, 0.5f, 1.0f)
        .endColor(0.8f, 1.0f, 0.2f, 0.0f)
        .clearColor(0.02f, 0.02f, 0.05f);

    // Turbulent particles
    chain.add<Particles>("turbulent")
        .emitterShape(Particles::Rectangle)
        .emitPosition(0.5f, 0.5f)
        .emitterSize(0.6f, 0.4f)
        .emitRate(50.0f)
        .life(3.0f)
        .speed(0.05f)
        .gravity(0.0f)
        .turbulence(0.3f)
        .turbulenceScale(3.0f)
        .startSize(0.01f)
        .endSize(0.02f)
        .startColor(1.0f, 0.5f, 1.0f, 0.8f)
        .endColor(0.5f, 0.2f, 1.0f, 0.0f)
        .clearColor(0.02f, 0.02f, 0.05f);

    // Composite all effects
    chain.add<Composite>("combined")
        .input("fire")
        .blend("fountain", Composite::Add, 1.0f)
        .blend("ring", Composite::Add, 1.0f)
        .blend("turbulent", Composite::Add, 0.5f);

    chain.setOutput("combined");
}

void update(Chain& chain, Context& ctx) {
    // Animate fire position side to side
    float fireX = 0.5f + 0.2f * std::sin(ctx.time() * 0.5f);
    chain.get<Particles>("fire").emitPosition(fireX, 0.9f);

    // Pulsing emit rate for fountain
    float rate = 60.0f + 40.0f * std::sin(ctx.time() * 2.0f);
    chain.get<Particles>("fountain").emitRate(rate);

    // Rotating ring emitter
    float ringX = 0.5f + 0.15f * std::cos(ctx.time() * 0.8f);
    float ringY = 0.5f + 0.15f * std::sin(ctx.time() * 0.8f);
    chain.get<Particles>("ring").emitPosition(ringX, ringY);

    // Cycle through demos every 5 seconds
    int demo = static_cast<int>(ctx.time() / 5.0f) % 5;
    switch (demo) {
        case 0: chain.setOutput("fire"); break;
        case 1: chain.setOutput("fountain"); break;
        case 2: chain.setOutput("ring"); break;
        case 3: chain.setOutput("turbulent"); break;
        case 4: chain.setOutput("combined"); break;
    }
}

VIVID_CHAIN(setup, update)
