// Vortex Force Test
// Demonstrates VortexForce with GPU simulation

#include <vivid/vivid.h>
#include <vivid/effects/particle_system.h>
#include <vivid/effects/forces/all_forces.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace vivid;
using namespace vivid::effects;

float cameraAngle = 0.0f;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    auto& ps = chain.add<ParticleSystem>("particles");

    // GPU simulation in 3D
    ps.space(ParticleSpace::World3D);
    ps.simulation(SimulationMode::GPU);
    ps.rendering(RenderMode::Circle);

    // High particle count
    ps.maxParticles = 80000;

    // Emit from a ring around the vortex
    ps.emitter(PsEmitterShape::Ring);
    ps.emitterPosition.set(0.0f, 0.0f, 0.0f);
    ps.emitterSize = 1.5f;  // Ring radius
    ps.emitRate = 5000.0f;

    // Particle properties
    ps.lifeMin = 4.0f;
    ps.lifeMax = 6.0f;
    ps.sizeStart = 0.03f;
    ps.sizeEnd = 0.01f;
    ps.initialVelocity.set(0.0f, 0.0f, 0.0f);  // Start stationary
    ps.velocityVariation = 0.0f;

    // Forces
    ps.clearForces();

    // Vortex - main rotation around Y axis
    auto& vortex = ps.addForce<VortexForce>();
    vortex.center.set(0.0f, 0.0f, 0.0f);
    vortex.axis.set(0.0f, 1.0f, 0.0f);  // Rotate around Y
    vortex.strength = 2.0f;
    vortex.falloff = 0.5f;  // Slower falloff = more uniform rotation

    // Slight upward drift
    auto& grav = ps.addForce<GravityForce>();
    grav.direction.set(0.0f, 0.1f, 0.0f);

    // Drag to prevent runaway speed
    auto& drag = ps.addForce<DragForce>();
    drag.coefficient = 0.1f;

    // Colors - warm spiral
    ps.colorMode(PsColorMode::Gradient);
    ps.colorStart.set(1.0f, 0.6f, 0.2f, 1.0f);  // Orange
    ps.colorEnd.set(1.0f, 0.2f, 0.4f, 0.0f);    // Red fade

    // Dark background
    ps.clearColor.set(0.02f, 0.02f, 0.05f, 1.0f);

    chain.output("particles");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& ps = chain.get<ParticleSystem>("particles");

    // Orbit camera
    cameraAngle += static_cast<float>(ctx.dt()) * 0.3f;

    glm::vec3 cameraPos(
        std::sin(cameraAngle) * 4.0f,
        2.0f,
        std::cos(cameraAngle) * 4.0f
    );

    glm::mat4 view = glm::lookAt(cameraPos, glm::vec3(0.0f, 0.5f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f/9.0f, 0.1f, 100.0f);

    glm::vec3 cameraRight = glm::vec3(view[0][0], view[1][0], view[2][0]);
    glm::vec3 cameraUp = glm::vec3(view[0][1], view[1][1], view[2][1]);

    ps.setCamera(view, proj, cameraRight, cameraUp);
}

VIVID_CHAIN(setup, update)
