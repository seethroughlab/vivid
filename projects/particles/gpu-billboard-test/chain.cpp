// GPU ParticleSystem Billboard Test
// Tests GPU compute + GPU-direct billboard rendering

#include <vivid/vivid.h>
#include <vivid/effects/particle_system.h>
#include <vivid/effects/forces/all_forces.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace vivid;
using namespace vivid::effects;

// Simple orbit camera
float cameraAngle = 0.0f;
glm::vec3 cameraTarget(0.0f, 0.0f, 0.0f);
float cameraDistance = 3.0f;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Create unified particle system with GPU simulation
    auto& ps = chain.add<ParticleSystem>("particles");

    // Enable GPU simulation and Billboard rendering
    ps.space(ParticleSpace::World3D);
    ps.simulation(SimulationMode::GPU);  // GPU compute!
    ps.rendering(RenderMode::Billboard);

    // High particle count (50K)
    ps.maxParticles = 50000;

    // Sphere emitter in 3D
    ps.emitter(PsEmitterShape::Sphere);
    ps.emitterPosition.set(0.0f, 0.0f, 0.0f);
    ps.emitterSize = 0.3f;
    ps.emitRate = 5000.0f;

    // Particle properties
    ps.lifeMin = 3.0f;
    ps.lifeMax = 5.0f;
    ps.sizeStart = 0.08f;
    ps.sizeEnd = 0.02f;

    // Curl noise for organic motion
    auto& curl = ps.addForce<CurlNoiseForce>();
    curl.strength = 0.8f;
    curl.scale = 2.0f;
    curl.speed = 0.3f;
    curl.octaves = 3;

    // Upward drift
    auto& grav = ps.addForce<GravityForce>();
    grav.direction.set(0.0f, 0.1f, 0.0f);

    auto& drag = ps.addForce<DragForce>();
    drag.coefficient = 0.2f;

    // Fire-like gradient
    ps.colorMode(PsColorMode::Gradient);
    ps.colorStart.set(1.0f, 0.8f, 0.2f, 1.0f);  // Yellow
    ps.colorEnd.set(1.0f, 0.2f, 0.1f, 0.0f);    // Red fade
    ps.fadeOut = true;

    // Dark background
    ps.clearColor.set(0.02f, 0.02f, 0.05f, 1.0f);

    chain.output("particles");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& ps = chain.get<ParticleSystem>("particles");

    // Animate camera
    cameraAngle += static_cast<float>(ctx.dt()) * 0.3f;

    glm::vec3 cameraPos(
        std::sin(cameraAngle) * cameraDistance,
        1.0f,
        std::cos(cameraAngle) * cameraDistance
    );

    // Compute camera matrices
    glm::mat4 view = glm::lookAt(cameraPos, cameraTarget, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f/9.0f, 0.1f, 100.0f);

    // Extract camera vectors for billboarding
    glm::vec3 cameraRight = glm::vec3(view[0][0], view[1][0], view[2][0]);
    glm::vec3 cameraUp = glm::vec3(view[0][1], view[1][1], view[2][1]);

    ps.setCamera(view, proj, cameraRight, cameraUp);
}

VIVID_CHAIN(setup, update)
