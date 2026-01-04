// Force Stack Test
// Tests the new modular force system with CPU simulation

#include <vivid/vivid.h>
#include <vivid/effects/particle_system.h>
#include <vivid/effects/forces/all_forces.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    auto& ps = chain.add<ParticleSystem>("particles");

    // Use CPU simulation for testing
    ps.space(ParticleSpace::Screen2D);
    ps.simulation(SimulationMode::CPU);
    ps.rendering(RenderMode::Circle);

    // Emission
    ps.maxParticles = 5000;
    ps.emitter(PsEmitterShape::Disc);
    ps.emitterPosition.set(0.5f, 0.5f, 0.0f);
    ps.emitterSize = 0.05f;
    ps.emitRate = 500.0f;

    // Particle properties
    ps.lifeMin = 2.0f;
    ps.lifeMax = 4.0f;
    ps.sizeStart = 0.015f;
    ps.sizeEnd = 0.005f;

    // === Use the new force stack API! ===
    ps.clearForces();  // Remove any default forces

    // Add curl noise force
    auto& curl = ps.addForce<CurlNoiseForce>();
    curl.strength = 1.2f;
    curl.scale = 3.0f;
    curl.speed = 0.4f;
    curl.octaves = 3;
    curl.is3D = false;  // 2D mode

    // Add drag
    auto& drag = ps.addForce<DragForce>();
    drag.coefficient = 0.3f;

    // Add gravity (slight upward drift)
    auto& grav = ps.addForce<GravityForce>();
    grav.direction.set(0.0f, 0.05f, 0.0f);

    // Colors
    ps.colorMode(PsColorMode::Gradient);
    ps.colorStart.set(0.2f, 0.8f, 1.0f, 1.0f);  // Cyan
    ps.colorEnd.set(0.8f, 0.2f, 1.0f, 0.0f);    // Magenta fade

    // Dark background
    ps.clearColor.set(0.02f, 0.02f, 0.05f, 1.0f);

    chain.output("particles");
}

void update(Context& ctx) {
    // Nothing special needed - forces applied automatically
}

VIVID_CHAIN(setup, update)
