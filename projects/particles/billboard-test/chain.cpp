// Billboard ParticleSystem Test
// Tests 3D billboard rendering with camera-facing quads

#include <vivid/vivid.h>
#include <vivid/effects/particle_system.h>
#include <vivid/render3d/render3d.h>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

// Global camera for accessing in update()
CameraOperator* g_camera = nullptr;
ParticleSystem* g_particles = nullptr;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Create orbiting camera
    auto& camera = chain.add<CameraOperator>("camera");
    camera.orbitCenter(0.0f, 0.0f, 0.0f);
    camera.distance(8.0f);
    camera.elevation(0.3f);
    g_camera = &camera;

    // Create particle system with Billboard rendering
    auto& ps = chain.add<ParticleSystem>("particles");
    ps.space(ParticleSpace::World3D);
    ps.simulation(SimulationMode::CPU);
    ps.rendering(RenderMode::Billboard);
    g_particles = &ps;

    // Emitter configuration - sphere emitter at origin
    ps.emitter(PsEmitterShape::Sphere);
    ps.emitterPosition.set(0.0f, 0.0f, 0.0f);
    ps.emitterSize = 0.5f;
    ps.emitRate = 200.0f;

    // Particle properties
    ps.lifeMin = 2.0f;
    ps.lifeMax = 4.0f;
    ps.sizeStart = 0.15f;
    ps.sizeEnd = 0.02f;

    // Upward velocity with radial spread
    ps.initialVelocity.set(0.0f, 1.5f, 0.0f);
    ps.radialVelocity = 0.5f;
    ps.spread = 45.0f;

    // Gravity and turbulence for organic motion
    ps.gravity.set(0.0f, -0.5f, 0.0f);
    ps.turbulence = 0.3f;

    // Color gradient - fire-like
    ps.colorMode(PsColorMode::Gradient);
    ps.colorStart.set(1.0f, 0.8f, 0.2f, 1.0f);
    ps.colorEnd.set(1.0f, 0.0f, 0.0f, 0.0f);
    ps.fadeOut = true;

    // Dark background
    ps.clearColor.set(0.02f, 0.02f, 0.05f, 1.0f);

    chain.output("particles");
}

void update(Context& ctx) {
    // Animate camera orbit
    float time = static_cast<float>(ctx.time());
    g_camera->azimuth(time * 0.5f);

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
