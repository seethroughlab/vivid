// Mesh ParticleSystem Test
// Tests velocity-aligned instanced mesh rendering (like gpu-curl-flow)

#include <vivid/vivid.h>
#include <vivid/effects/particle_system.h>
#include <vivid/effects/forces/all_forces.h>
#include <vivid/render3d/render3d.h>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

// Global references for update()
CameraOperator* g_camera = nullptr;
ParticleSystem* g_particles = nullptr;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Create orbiting camera
    auto& camera = chain.add<CameraOperator>("camera");
    camera.orbitCenter(0.0f, 0.0f, 0.0f);
    camera.distance(6.0f);
    camera.elevation(0.4f);
    camera.fov(50.0f);
    g_camera = &camera;

    // Create particle system with Mesh rendering
    auto& ps = chain.add<ParticleSystem>("particles");
    ps.space(ParticleSpace::World3D);
    ps.simulation(SimulationMode::CPU);
    ps.rendering(RenderMode::Mesh);
    g_particles = &ps;

    // Use built-in elongated cube (good for velocity trails)
    ps.useBuiltinCube(0.015f, 0.08f);  // width, length
    ps.alignToVelocity(true);  // Align cubes to velocity direction

    // Emitter - sphere at origin
    ps.emitter(PsEmitterShape::Sphere);
    ps.emitterPosition.set(0.0f, 0.0f, 0.0f);
    ps.emitterSize = 0.5f;
    ps.emitRate = 3000.0f;
    ps.maxParticles = 20000;

    // Particle lifetime
    ps.lifeMin = 4.0f;
    ps.lifeMax = 8.0f;

    // Size (affects cube scale)
    ps.sizeStart = 1.0f;
    ps.sizeEnd = 0.5f;

    // Curl noise for organic flow
    auto& curl = ps.addForce<CurlNoiseForce>();
    curl.strength = 2.0f;
    curl.scale = 0.6f;
    curl.speed = 0.03f;
    curl.octaves = 2;

    // Light drag
    auto& drag = ps.addForce<DragForce>();
    drag.coefficient = 0.02f;

    // Deep red color (like the gpu-curl-flow reference)
    ps.colorMode(PsColorMode::Gradient);
    ps.colorStart.set(1.0f, 0.15f, 0.1f, 1.0f);
    ps.colorEnd.set(0.5f, 0.0f, 0.0f, 0.3f);
    ps.fadeOut = true;

    // Dark background
    ps.clearColor.set(0.02f, 0.02f, 0.03f, 1.0f);

    chain.output("particles");
}

void update(Context& ctx) {
    float time = static_cast<float>(ctx.time());

    // Slowly orbit camera
    g_camera->azimuth(time * 0.2f);

    // Update particle system with camera data
    const Camera3D& cam = g_camera->outputCamera();
    g_particles->setCamera(
        cam.viewMatrix(),
        cam.projectionMatrix(),
        cam.right(),
        cam.getUp()
    );
}

VIVID_CHAIN(setup, update)
