// GPU Force Stack Test
// Tests the modular force system with GPU compute simulation

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

    // Use GPU simulation with force stack
    ps.space(ParticleSpace::World3D);
    ps.simulation(SimulationMode::GPU);  // GPU compute!
    ps.rendering(RenderMode::Circle);

    // High particle count
    ps.maxParticles = 100000;

    // Sphere emitter
    ps.emitter(PsEmitterShape::Sphere);
    ps.emitterPosition.set(0.0f, 0.0f, 0.0f);
    ps.emitterSize = 0.2f;
    ps.emitRate = 8000.0f;

    // Particle properties
    ps.lifeMin = 3.0f;
    ps.lifeMax = 5.0f;
    ps.sizeStart = 0.04f;
    ps.sizeEnd = 0.01f;

    // === Use the new force stack API with GPU! ===
    ps.clearForces();

    // Curl noise - primary motion
    auto& curl = ps.addForce<CurlNoiseForce>();
    curl.strength = 1.0f;
    curl.scale = 2.5f;
    curl.speed = 0.3f;
    curl.octaves = 4;

    // Drag - slow particles over time
    auto& drag = ps.addForce<DragForce>();
    drag.coefficient = 0.2f;

    // Gravity - subtle upward drift
    auto& grav = ps.addForce<GravityForce>();
    grav.direction.set(0.0f, 0.15f, 0.0f);

    // Point attractor - pull toward center
    auto& att = ps.addForce<PointAttractorForce>();
    att.position.set(0.0f, 0.0f, 0.0f);
    att.strength = 0.3f;

    // Colors - green to blue gradient
    ps.colorMode(PsColorMode::Gradient);
    ps.colorStart.set(0.3f, 1.0f, 0.5f, 1.0f);
    ps.colorEnd.set(0.2f, 0.5f, 1.0f, 0.0f);
    ps.fadeOut = true;

    // Dark background
    ps.clearColor.set(0.02f, 0.02f, 0.05f, 1.0f);

    chain.output("particles");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& ps = chain.get<ParticleSystem>("particles");

    // Animate camera
    cameraAngle += static_cast<float>(ctx.dt()) * 0.2f;

    glm::vec3 cameraPos(
        std::sin(cameraAngle) * 3.0f,
        1.0f,
        std::cos(cameraAngle) * 3.0f
    );

    glm::mat4 view = glm::lookAt(cameraPos, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f/9.0f, 0.1f, 100.0f);

    glm::vec3 cameraRight = glm::vec3(view[0][0], view[1][0], view[2][0]);
    glm::vec3 cameraUp = glm::vec3(view[0][1], view[1][1], view[2][1]);

    ps.setCamera(view, proj, cameraRight, cameraUp);
}

VIVID_CHAIN(setup, update)
