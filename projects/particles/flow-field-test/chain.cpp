// Velocity Field Test
// Demonstrates VelocityFieldForce (CPU mode)

#include <vivid/vivid.h>
#include <vivid/effects/particle_system.h>
#include <vivid/effects/forces/all_forces.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    auto& ps = chain.add<ParticleSystem>("particles");

    // CPU simulation (VelocityFieldForce is CPU-only)
    ps.space(ParticleSpace::Screen2D);
    ps.simulation(SimulationMode::CPU);
    ps.rendering(RenderMode::Circle);

    ps.maxParticles = 10000;

    // Emit across screen
    ps.emitter(PsEmitterShape::Rectangle);
    ps.emitterPosition.set(0.5f, 0.5f, 0.0f);
    ps.emitterSize = 0.4f;
    ps.emitRate = 2000.0f;

    // Particle properties
    ps.lifeMin = 3.0f;
    ps.lifeMax = 5.0f;
    ps.sizeStart = 0.01f;
    ps.sizeEnd = 0.005f;

    // Forces
    ps.clearForces();

    // Spiral flow field
    auto& flow = ps.addForce<VelocityFieldForce>();
    flow.mode = VelocityFieldMode::Spiral;
    flow.center.set(0.5f, 0.5f, 0.0f);
    flow.strength = 0.8f;
    flow.scale = 2.0f;

    // Light drag
    auto& drag = ps.addForce<DragForce>();
    drag.coefficient = 0.2f;

    // Colors - spectrum
    ps.colorMode(PsColorMode::Gradient);
    ps.colorStart.set(1.0f, 0.4f, 0.4f, 1.0f);  // Red
    ps.colorEnd.set(0.4f, 0.4f, 1.0f, 0.0f);    // Blue fade

    ps.clearColor.set(0.02f, 0.02f, 0.05f, 1.0f);

    chain.output("particles");
}

void update(Context& ctx) {
    // Nothing needed
}

VIVID_CHAIN(setup, update)
