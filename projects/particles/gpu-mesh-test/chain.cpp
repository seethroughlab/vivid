// GPU ParticleSystem Mesh Test
// Tests GPU compute + GPU-direct velocity-aligned mesh rendering

#include <vivid/vivid.h>
#include <vivid/effects/particle_system.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace vivid;
using namespace vivid::effects;

// Simple orbit camera
float cameraAngle = 0.0f;
glm::vec3 cameraTarget(0.0f, 0.0f, 0.0f);
float cameraDistance = 4.0f;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Create unified particle system with GPU simulation
    auto& ps = chain.add<ParticleSystem>("particles");

    // Enable GPU simulation and Mesh rendering
    ps.space(ParticleSpace::World3D);
    ps.simulation(SimulationMode::GPU);  // GPU compute!
    ps.rendering(RenderMode::Mesh);

    // Use builtin elongated cube mesh
    ps.useBuiltinCube(0.01f, 0.08f);  // thin elongated cubes
    ps.alignToVelocity(true);

    // High particle count (50K)
    ps.maxParticles = 50000;

    // Ring emitter
    ps.emitter(PsEmitterShape::Ring);
    ps.emitterPosition.set(0.0f, 0.0f, 0.0f);
    ps.emitterSize = 0.8f;
    ps.emitRate = 6000.0f;

    // Velocity - outward burst
    ps.radialVelocity = 0.5f;
    ps.velocityVariation = 0.3f;

    // Particle properties
    ps.lifeMin = 3.0f;
    ps.lifeMax = 5.0f;
    ps.sizeStart = 1.5f;
    ps.sizeEnd = 0.3f;

    // Strong curl noise
    ps.curlStrength = 1.5f;
    ps.curlScale = 2.5f;
    ps.curlSpeed = 0.4f;
    ps.curlOctaves = 4;

    // Light drag
    ps.drag = 0.15f;

    // Cyan to magenta gradient
    ps.colorMode(PsColorMode::Gradient);
    ps.colorStart.set(0.2f, 1.0f, 0.9f, 1.0f);
    ps.colorEnd.set(1.0f, 0.3f, 0.8f, 0.0f);
    ps.fadeOut = true;

    // Dark background
    ps.clearColor.set(0.02f, 0.02f, 0.05f, 1.0f);

    chain.output("particles");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& ps = chain.get<ParticleSystem>("particles");

    // Animate camera
    cameraAngle += static_cast<float>(ctx.dt()) * 0.25f;

    glm::vec3 cameraPos(
        std::sin(cameraAngle) * cameraDistance,
        1.5f,
        std::cos(cameraAngle) * cameraDistance
    );

    // Compute camera matrices
    glm::mat4 view = glm::lookAt(cameraPos, cameraTarget, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f/9.0f, 0.1f, 100.0f);

    // Extract camera vectors
    glm::vec3 cameraRight = glm::vec3(view[0][0], view[1][0], view[2][0]);
    glm::vec3 cameraUp = glm::vec3(view[0][1], view[1][1], view[2][1]);

    ps.setCamera(view, proj, cameraRight, cameraUp);
}

VIVID_CHAIN(setup, update)
