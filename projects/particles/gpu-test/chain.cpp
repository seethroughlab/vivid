// GPU ParticleSystem Test
// Tests the new GPU compute simulation with 100K particles

#include <vivid/vivid.h>
#include <vivid/effects/particle_system.h>
#include <vivid/effects/forces/all_forces.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Create unified particle system with GPU simulation
    auto& ps = chain.add<ParticleSystem>("particles");

    // Enable GPU simulation for high particle counts
    ps.space(ParticleSpace::Screen2D);
    ps.simulation(SimulationMode::GPU);  // GPU compute!
    ps.rendering(RenderMode::Circle);

    // High particle count (100K)
    ps.maxParticles = 100000;

    // Emitter configuration
    ps.emitter(PsEmitterShape::Disc);
    ps.emitterPosition.set(0.5f, 0.5f, 0.0f);
    ps.emitterSize = 0.15f;
    ps.emitRate = 8000.0f;  // High emission rate

    // Particle properties
    ps.lifeMin = 3.0f;
    ps.lifeMax = 6.0f;
    ps.sizeStart = 0.01f;
    ps.sizeEnd = 0.003f;

    // Curl noise for organic motion
    auto& curl = ps.addForce<CurlNoiseForce>();
    curl.strength = 1.0f;
    curl.scale = 3.0f;
    curl.speed = 0.2f;
    curl.octaves = 3;

    // Light drag
    auto& drag = ps.addForce<DragForce>();
    drag.coefficient = 0.1f;

    // Color gradient - blue to purple
    ps.colorMode(PsColorMode::Gradient);
    ps.colorStart.set(0.2f, 0.6f, 1.0f, 1.0f);
    ps.colorEnd.set(0.8f, 0.2f, 1.0f, 0.0f);
    ps.fadeOut = true;

    // Dark background
    ps.clearColor.set(0.02f, 0.02f, 0.05f, 1.0f);

    chain.output("particles");
}

void update(Context& ctx) {
    // Animation updates happen automatically in ParticleSystem::process()
}

VIVID_CHAIN(setup, update)
