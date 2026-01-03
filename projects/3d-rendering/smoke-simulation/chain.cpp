// Smoke Simulation - 3D Particles with Spritesheet Animation
// Demonstrates billboarded smoke particles with animated sprites

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
    camera.orbitCenter(0, 1.5f, 0);
    camera.distance(10.0f);
    camera.elevation(0.3f);
    camera.azimuth(0.0f);
    camera.fov(50.0f);

    // =========================================================================
    // Smoke Particle System
    // =========================================================================
    auto& smoke = chain.add<Particles3D>("smoke");

    // Emitter: disc at ground level
    smoke.emitter(Emitter3DShape::Disc);
    smoke.position(0.0f, 0.0f, 0.0f);
    smoke.emitterSize(0.8f);  // Disc radius

    // Emission rate
    smoke.emitRate(15.0f);  // Sparse, puffy smoke
    smoke.maxParticles(200);

    // Velocity: gentle upward drift
    smoke.velocity(0.0f, 1.5f, 0.0f);
    smoke.radialVelocity(0.3f);
    smoke.spread(25.0f);
    smoke.velocityVariation(0.3f);

    // Physics: no gravity (smoke rises), heavy drag, turbulence
    smoke.gravity(0.0f, 0.2f, 0.0f);  // Slight buoyancy
    smoke.drag(0.3f);
    smoke.turbulence(0.8f);

    // Lifetime: long-lived smoke puffs
    smoke.life(4.0f);
    smoke.lifeVariation(0.3f);

    // Size: start small, grow as smoke expands
    smoke.size(0.5f, 2.5f);
    smoke.sizeVariation(0.3f);

    // Color: white/light gray smoke, fading out
    smoke.color(1.0f, 1.0f, 1.0f, 0.9f);
    smoke.colorEnd(0.8f, 0.8f, 0.8f, 0.0f);

    // Texture: smoke spritesheet
    smoke.texture("assets/textures/Smoke30Frames.png");
    smoke.spriteSheet(6, 5);    // 6 columns, 5 rows
    smoke.spriteFrames(30);     // 30 total frames
    smoke.spriteAnimateByLife(false); // Time-based animation, not lifetime
    smoke.spriteFPS(12.0f);           // Loop at 12 FPS
    smoke.spriteRandomStart(true);    // Random starting frame per particle

    // Slow rotation for organic feel
    smoke.spin(0.3f);

    // Rendering
    smoke.fadeOut(true);
    smoke.additive(false);  // Normal blending for smoke
    smoke.depthSort(true);
    smoke.clearColor(0.2f, 0.3f, 0.4f, 1.0f);  // Sky blue background

    // Connect camera
    smoke.setCameraInput(&camera);

    chain.output("smoke");

    std::cout << "\n========================================" << std::endl;
    std::cout << "Smoke Simulation" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "3D particles with 30-frame spritesheet" << std::endl;
    std::cout << "Billboarded, depth-sorted, turbulent" << std::endl;
    std::cout << "========================================\n" << std::endl;
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& camera = chain.get<CameraOperator>("camera");

    // Slow camera orbit
    float time = static_cast<float>(ctx.time());
    camera.azimuth(time * 0.15f);
}

VIVID_CHAIN(setup, update)
