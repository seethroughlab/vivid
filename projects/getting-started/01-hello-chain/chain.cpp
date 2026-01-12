// Lesson 01: Hello Chain
// Your first Vivid project - a simple animated noise pattern
//
// Run: ./build/bin/vivid projects/getting-started/01-hello-chain
//
// Try editing this file while it's running - changes apply instantly!

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Create a noise generator
    // Noise creates animated procedural patterns
    auto& noise = chain.add<Noise>("noise");
    noise.scale = 4.0f;    // Pattern size (try 1.0 to 20.0)
    noise.speed = 0.5f;    // Animation speed
    noise.octaves = 4;     // Detail layers (1-8)

    // EXPERIMENT: Uncomment to add color
    // auto& hsv = chain.add<HSV>("hsv");
    // hsv.input("noise");
    // hsv.hueShift = 0.5f;
    // hsv.saturation = 1.5f;
    // chain.output("hsv");

    // Tell Vivid what to display
    chain.output("noise");
}

void update(Context& ctx) {
    // Toggle fullscreen with F key
    if (ctx.key(GLFW_KEY_F).pressed) {
        ctx.fullscreen(!ctx.fullscreen());
    }

    // EXPERIMENT: Uncomment to animate the scale
    // auto& noise = ctx.chain().get<Noise>("noise");
    // noise.scale = 4.0f + sin(ctx.time()) * 2.0f;
}

// Export the chain for the Vivid runtime
VIVID_CHAIN(setup, update)
