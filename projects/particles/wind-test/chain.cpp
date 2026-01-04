// Wind Force Test
// Demonstrates WindForce with gusts

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

    ps.maxParticles = 50000;

    // Emit from left side
    ps.emitter(PsEmitterShape::Box);
    ps.emitterPosition.set(-2.0f, 0.0f, 0.0f);
    ps.emitterSize = 0.5f;
    ps.emitRate = 3000.0f;

    // Particle properties
    ps.lifeMin = 3.0f;
    ps.lifeMax = 5.0f;
    ps.sizeStart = 0.04f;
    ps.sizeEnd = 0.015f;

    // Forces
    ps.clearForces();

    // Wind blowing right with gusts
    auto& wind = ps.addForce<WindForce>();
    wind.direction.set(1.0f, 0.0f, 0.0f);  // Rightward
    wind.strength = 1.5f;
    wind.gustStrength = 0.5f;
    wind.gustFrequency = 2.0f;

    // Slight downward gravity
    auto& grav = ps.addForce<GravityForce>();
    grav.direction.set(0.0f, -0.1f, 0.0f);

    // Colors - sky blue to white
    ps.colorMode(PsColorMode::Gradient);
    ps.colorStart.set(0.6f, 0.8f, 1.0f, 1.0f);
    ps.colorEnd.set(1.0f, 1.0f, 1.0f, 0.0f);

    ps.clearColor.set(0.1f, 0.15f, 0.25f, 1.0f);

    chain.output("particles");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& ps = chain.get<ParticleSystem>("particles");

    // Static camera
    glm::vec3 cameraPos(0.0f, 0.5f, 5.0f);

    glm::mat4 view = glm::lookAt(cameraPos, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f/9.0f, 0.1f, 100.0f);

    glm::vec3 cameraRight = glm::vec3(view[0][0], view[1][0], view[2][0]);
    glm::vec3 cameraUp = glm::vec3(view[0][1], view[1][1], view[2][1]);

    ps.setCamera(view, proj, cameraRight, cameraUp);
}

VIVID_CHAIN(setup, update)
