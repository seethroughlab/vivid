// Unified ParticleSystem Test
// Minimal test of ParticleSystem with CPU simulation and Circle rendering

#include <vivid/vivid.h>
#include <vivid/effects/particle_system.h>
#include <vivid/effects/forces/all_forces.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    auto& ps = chain.add<ParticleSystem>("particles");

    // Basic 2D setup
    ps.space(ParticleSpace::Screen2D);
    ps.simulation(SimulationMode::CPU);
    ps.rendering(RenderMode::Circle);

    // Simple emitter
    ps.emitter(PsEmitterShape::Disc);
    ps.emitterPosition.set(0.5f, 0.5f, 0.0f);
    ps.emitterSize = 0.1f;
    ps.emitRate = 200.0f;

    // Particle properties
    ps.lifeMin = 2.0f;
    ps.lifeMax = 3.0f;
    ps.sizeStart = 0.02f;
    ps.sizeEnd = 0.008f;

    // Simple curl noise
    auto& curl = ps.addForce<CurlNoiseForce>();
    curl.strength = 0.5f;
    curl.scale = 4.0f;

    // Color gradient
    ps.colorMode(PsColorMode::Gradient);
    ps.colorStart.set(1.0f, 0.6f, 0.2f, 1.0f);
    ps.colorEnd.set(1.0f, 0.2f, 0.1f, 0.0f);

    ps.clearColor.set(0.05f, 0.05f, 0.1f, 1.0f);

    chain.output("particles");
}

void update(Context& ctx) {
}

VIVID_CHAIN(setup, update)
