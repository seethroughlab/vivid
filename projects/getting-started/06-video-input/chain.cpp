// Lesson 06: Video Input
// Use webcam as a texture source with live effects
//
// Run: ./build/bin/vivid projects/getting-started/06-video-input
//
// Requires a webcam connected to your computer.

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/video/video.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================
    // Video Source - Webcam
    // =========================================

    auto& cam = chain.add<vivid::video::Webcam>("cam");
    cam.setResolution(1280, 720);
    cam.setFrameRate(30.0f);

    // =========================================
    // Displacement Map - Animated Noise
    // =========================================

    // Create noise to use as a displacement map
    auto& noise = chain.add<Noise>("noise");
    noise.scale = 4.0f;      // Size of the waves
    noise.speed = 0.4f;      // Animation speed
    noise.octaves = 2;

    // =========================================
    // Effect - Displace the video
    // =========================================

    auto& displace = chain.add<Displace>("displace");
    displace.source("cam");      // What to distort
    displace.map("noise");       // What drives the distortion
    displace.strength = 0.04f;   // How much (0.01 = subtle, 0.1 = strong)

    // =========================================
    // Post-processing
    // =========================================

    // Add a vignette for polish
    auto& vignette = chain.add<Vignette>("vignette");
    vignette.input("displace");
    vignette.radius = 0.7f;
    vignette.softness = 0.6f;

    // EXPERIMENT: Try adding more effects
    // auto& hsv = chain.add<HSV>("hsv");
    // hsv.input("vignette");
    // hsv.saturation = 1.3f;
    // chain.output("hsv");

    chain.output("vignette");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& displace = chain.get<Displace>("displace");
    auto& noise = chain.get<Noise>("noise");

    // Mouse controls displacement
    glm::vec2 mouse = ctx.mouseNorm();

    // X position controls displacement strength
    displace.strength = 0.01f + mouse.x * 0.1f;

    // Y position controls noise scale
    noise.scale = 2.0f + mouse.y * 8.0f;

    // Fullscreen toggle
    if (ctx.key(GLFW_KEY_F).pressed) {
        ctx.fullscreen(!ctx.fullscreen());
    }

    // Space pauses the noise animation
    static bool animating = true;
    if (ctx.key(GLFW_KEY_SPACE).pressed) {
        animating = !animating;
    }
    noise.speed = animating ? 0.4f : 0.0f;
}

VIVID_CHAIN(setup, update)
