// Particles3D Test - 3D GPU Particle System
// Demonstrates billboarded particles with world-space physics

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/render3d/render3d.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::render3d;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // Camera
    // =========================================================================
    auto& camera = chain.add<CameraOperator>("camera");
    camera.orbitCenter(0, 0, 0);
    camera.distance(12.0f);
    camera.elevation(0.4f);
    camera.azimuth(0.0f);
    camera.fov(50.0f);

    // =========================================================================
    // Particle System - Fire/Embers effect
    // =========================================================================
    auto& particles = chain.add<Particles3D>("particles");

    // Emitter: cone pointing up (like a fire)
    particles.emitter(Emitter3DShape::Cone);
    particles.position(0.0f, -2.0f, 0.0f);  // Base at bottom
    particles.emitterSize(0.5f);             // Cone base radius
    particles.emitterDirection(0.0f, 1.0f, 0.0f);  // Pointing up
    particles.coneAngle(20.0f);              // Spread angle

    // Emission rate
    particles.emitRate(150.0f);
    particles.maxParticles(3000);

    // Velocity: upward with some spread
    particles.velocity(0.0f, 3.0f, 0.0f);
    particles.radialVelocity(0.5f);
    particles.spread(30.0f);
    particles.velocityVariation(0.3f);

    // Physics: light gravity (fire rises), some turbulence
    particles.gravity(0.0f, -0.5f, 0.0f);  // Slight downward pull
    particles.drag(0.2f);
    particles.turbulence(1.5f);

    // Lifetime
    particles.life(2.5f);
    particles.lifeVariation(0.3f);

    // Size: start small, grow, then shrink
    particles.size(0.15f, 0.05f);
    particles.sizeVariation(0.2f);

    // Color: fire gradient (orange -> red -> dark)
    particles.color(1.0f, 0.6f, 0.1f, 1.0f);   // Bright orange
    particles.colorEnd(0.8f, 0.1f, 0.0f, 0.0f); // Dark red, fading out

    // Rendering
    particles.fadeOut(true);
    particles.additive(true);  // Glow effect
    particles.depthSort(true);
    particles.clearColor(0.05f, 0.05f, 0.1f, 1.0f);  // Dark blue background

    // Connect camera
    particles.setCameraInput(&camera);

    chain.output("particles");

    std::cout << "\n========================================" << std::endl;
    std::cout << "Particles3D Test - Fire Effect" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Cone emitter with 150 particles/sec" << std::endl;
    std::cout << "Turbulence and additive blending" << std::endl;
    std::cout << "========================================\n" << std::endl;
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& camera = chain.get<CameraOperator>("camera");

    // Slow camera orbit
    float time = static_cast<float>(ctx.time());
    camera.azimuth(time * 0.3f);
}

VIVID_CHAIN(setup, update)
