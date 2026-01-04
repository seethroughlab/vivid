// Unified ParticleSystem Test
// Tests the new ParticleSystem operator with CPU simulation and Circle rendering

#include <vivid/vivid.h>
#include <vivid/effects/particle_system.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Create unified particle system
    auto& ps = chain.add<ParticleSystem>("particles");

    // Configure for 2D screen space with curl noise
    ps.space(ParticleSpace::Screen2D);
    ps.simulation(SimulationMode::CPU);
    ps.rendering(RenderMode::Circle);

    // Emitter configuration
    ps.emitter(PsEmitterShape::Disc);
    ps.emitterPosition.set(0.5f, 0.5f, 0.0f);  // Center of screen
    ps.emitterSize = 0.1f;
    ps.emitRate = 500.0f;

    // Particle properties
    ps.lifeMin = 2.0f;
    ps.lifeMax = 4.0f;
    ps.sizeStart = 0.02f;
    ps.sizeEnd = 0.005f;

    // Curl noise for organic motion
    ps.curlStrength = 0.5f;
    ps.curlScale = 4.0f;
    ps.curlSpeed = 0.3f;
    ps.curlOctaves = 3;

    // Color gradient
    ps.colorMode(PsColorMode::Gradient);
    ps.colorStart.set(1.0f, 0.6f, 0.2f, 1.0f);
    ps.colorEnd.set(1.0f, 0.0f, 0.0f, 0.0f);
    ps.fadeOut = true;

    // Dark background
    ps.clearColor.set(0.05f, 0.05f, 0.1f, 1.0f);

    chain.output("particles");
}

void update(Context& ctx) {
    // Animation updates happen automatically in ParticleSystem::process()
}

VIVID_CHAIN(setup, update)
