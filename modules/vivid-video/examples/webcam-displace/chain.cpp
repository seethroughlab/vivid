// Webcam Displacement
// Demonstrates combining video operators with generator operators
//
// Uses animated noise to spatially displace a webcam feed, creating
// wavy, liquid-like distortions in real-time.
//
// Controls:
//   Mouse X: Displacement strength
//   Mouse Y: Noise scale
//   Space: Pause noise animation
//   R: Reset to defaults

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/video/video.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

static bool animating = true;
static float strength = 0.05f;
static float noiseScale = 4.0f;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // Video Source - Webcam
    // =========================================================================

    auto& cam = chain.add<vivid::video::Webcam>("cam");
    cam.setResolution(1280, 720);
    cam.setFrameRate(30.0f);

    // =========================================================================
    // Generator - Animated Noise for Displacement Map
    // =========================================================================

    // Noise creates a displacement field - brighter areas push more
    auto& noise = chain.add<Noise>("noise");
    noise.scale = noiseScale;
    noise.speed = 0.5f;
    noise.octaves = 3;
    noise.type = NoiseType::Simplex;

    // =========================================================================
    // Effect - Displacement
    // =========================================================================

    // Displace uses the noise texture to shift pixels in the webcam
    // Red channel = X displacement, Green channel = Y displacement
    auto& displace = chain.add<Displace>("displace");
    displace.source("cam");
    displace.map("noise");
    displace.strength = strength;

    // =========================================================================
    // Post-processing
    // =========================================================================

    // Slight vignette for polish
    auto& vignette = chain.add<Vignette>("vignette");
    vignette.input("displace");
    vignette.intensity = 0.3f;
    vignette.softness = 0.5f;

    chain.output("vignette");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // Input Handling
    // =========================================================================

    // Space: Toggle animation
    if (ctx.key(GLFW_KEY_SPACE).pressed) {
        animating = !animating;
    }

    // R: Reset to defaults
    if (ctx.key(GLFW_KEY_R).pressed) {
        strength = 0.05f;
        noiseScale = 4.0f;
        animating = true;
    }

    // =========================================================================
    // Mouse Controls
    // =========================================================================

    glm::vec2 mouse = ctx.mouseNorm();

    // X axis: Displacement strength (0.01 to 0.15)
    strength = 0.01f + (mouse.x * 0.5f + 0.5f) * 0.14f;

    // Y axis: Noise scale (1.0 to 10.0)
    noiseScale = 1.0f + (mouse.y * 0.5f + 0.5f) * 9.0f;

    // =========================================================================
    // Update Operators
    // =========================================================================

    auto& noise = chain.get<Noise>("noise");
    auto& displace = chain.get<Displace>("displace");

    noise.scale = noiseScale;
    noise.speed = animating ? 0.5f : 0.0f;
    displace.strength = strength;
}

VIVID_CHAIN(setup, update)
