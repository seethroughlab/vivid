// Hello Noise - Vivid Example
// Demonstrates the basic Noise → Output chain
// Shows how to set explicit resolution on generators
//
// Controls:
//   F - Toggle fullscreen

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Configure noise generator with explicit resolution
    // Generators like Noise, Gradient, SolidColor use their declared resolution
    // (default is 1280x720 if not specified)
    auto& noise = chain.add<Noise>("noise");
    noise.setResolution(1920, 1080);  // Set to 1080p
    noise.scale = 4.0f;
    noise.speed = 0.5f;
    noise.octaves = 4;
    noise.lacunarity = 2.0f;
    noise.persistence = 0.5f;

    // Specify output - will be scaled to window size for display
    chain.output("noise");
}

void update(Context& ctx) {
    // Toggle fullscreen with F key
    if (ctx.key(GLFW_KEY_F).pressed) {
        ctx.fullscreen(!ctx.fullscreen());
    }

    // Debug value monitoring - visible in the debug panel (D key)
    ctx.debug("time", ctx.time());
    ctx.debug("frame", ctx.frame());
    ctx.debug("fps", 1.0f / ctx.dt());
}

VIVID_CHAIN(setup, update)
