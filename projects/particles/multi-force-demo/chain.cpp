// Multi-Force Demo
// Demonstrates the modular force stack with multiple combined forces
//
// This example creates a tornado-like effect using:
// - VortexForce: Main rotation around Y axis
// - GravityForce: Upward lift in the center
// - CurlNoiseForce: Organic turbulence
// - DragForce: Velocity damping
// - PointAttractorForce: Pulls particles toward center axis

#include <vivid/vivid.h>
#include <vivid/effects/particle_system.h>
#include <vivid/effects/forces/all_forces.h>
#include <vivid/effects/bloom.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace vivid;
using namespace vivid::effects;

float cameraAngle = 0.0f;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    auto& ps = chain.add<ParticleSystem>("particles");

    // GPU 3D simulation
    ps.space(ParticleSpace::World3D);
    ps.simulation(SimulationMode::GPU);
    ps.rendering(RenderMode::Circle);

    // Large particle count for dense effect
    ps.maxParticles = 150000;

    // Emit from ground disc
    ps.emitter(PsEmitterShape::Disc);
    ps.emitterPosition.set(0.0f, -1.0f, 0.0f);
    ps.emitterSize = 1.5f;
    ps.emitRate = 15000.0f;

    // Particle properties
    ps.lifeMin = 3.0f;
    ps.lifeMax = 6.0f;
    ps.sizeStart = 0.025f;
    ps.sizeEnd = 0.008f;

    // === MODULAR FORCE STACK ===
    ps.clearForces();

    // Vortex - main tornado rotation
    auto& vortex = ps.addForce<VortexForce>();
    vortex.center.set(0.0f, 0.0f, 0.0f);
    vortex.axis.set(0.0f, 1.0f, 0.0f);
    vortex.strength = 3.0f;
    vortex.falloff = 0.3f;  // Gentle falloff for wider rotation

    // Curl noise - organic turbulence
    auto& curl = ps.addForce<CurlNoiseForce>();
    curl.strength = 0.5f;
    curl.scale = 1.5f;
    curl.speed = 0.2f;
    curl.octaves = 3;

    // Gravity - upward lift
    auto& grav = ps.addForce<GravityForce>();
    grav.direction.set(0.0f, 1.5f, 0.0f);

    // Attractor - pulls toward center axis
    auto& att = ps.addForce<PointAttractorForce>();
    att.position.set(0.0f, 0.0f, 0.0f);
    att.strength = 0.5f;

    // Drag - prevents particles from going too fast
    auto& drag = ps.addForce<DragForce>();
    drag.coefficient = 0.15f;

    // Colors - warm gradient (white hot center to orange/red edges)
    ps.colorMode(PsColorMode::Gradient);
    ps.colorStart.set(1.0f, 0.9f, 0.7f, 1.0f);   // Bright warm
    ps.colorEnd.set(1.0f, 0.3f, 0.1f, 0.0f);     // Orange fade

    // Dark background
    ps.clearColor.set(0.02f, 0.02f, 0.04f, 1.0f);

    // Add bloom for glow effect
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("particles");
    bloom.threshold = 0.3f;
    bloom.intensity = 0.5f;
    bloom.radius = 1.5f;

    chain.output("bloom");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& ps = chain.get<ParticleSystem>("particles");

    // Slow orbit camera
    cameraAngle += static_cast<float>(ctx.dt()) * 0.15f;

    glm::vec3 cameraPos(
        std::sin(cameraAngle) * 6.0f,
        2.5f,
        std::cos(cameraAngle) * 6.0f
    );

    glm::mat4 view = glm::lookAt(cameraPos, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(55.0f), 16.0f/9.0f, 0.1f, 100.0f);

    glm::vec3 cameraRight = glm::vec3(view[0][0], view[1][0], view[2][0]);
    glm::vec3 cameraUp = glm::vec3(view[0][1], view[1][1], view[2][1]);

    ps.setCamera(view, proj, cameraRight, cameraUp);
}

VIVID_CHAIN(setup, update)
