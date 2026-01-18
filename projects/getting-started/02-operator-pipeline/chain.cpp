// Lesson 02: Operator Pipeline
// Chain multiple operators to build complex effects
//
// Run: ./build/bin/vivid projects/getting-started/02-operator-pipeline
//
// Press Tab to see the chain visualizer!

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // STAGE 1: Generate a base pattern (grayscale)
    auto& noise = chain.add<Noise>("noise");
    noise.scale = 6.0f;
    noise.speed = 0.3f;
    noise.octaves = 3;

    // STAGE 2: Soften with blur
    auto& blur = chain.add<Blur>("blur");
    blur.input("noise");      // Connect to "noise" operator
    blur.radius = 8.0f;       // Blur amount (pixels)

    // STAGE 3: Colorize using a gradient lookup table
    // The grayscale value from blur becomes the U coordinate
    // to sample colors from the gradient
    auto& gradient = chain.add<Gradient>("gradient");
    gradient.colorA.set(0.1f, 0.2f, 0.5f, 1.0f);  // Dark blue
    gradient.colorB.set(1.0f, 0.6f, 0.2f, 1.0f);  // Orange

    auto& colorize = chain.add<Lookup>("colorize");
    colorize.input("blur");       // Source: grayscale noise
    colorize.lut("gradient");     // LUT: color gradient

    // EXPERIMENT: Try adding more effects here
    // auto& mirror = chain.add<Mirror>("mirror");
    // mirror.input("colorize");
    // mirror.axis = MirrorAxis::Both;
    // chain.output("mirror");

    // Output the final result
    chain.output("colorize");
}

void update(Context& ctx) {
    if (ctx.key(GLFW_KEY_F).pressed) {
        ctx.fullscreen(!ctx.fullscreen());
    }

    // EXPERIMENT: Animate parameters
    auto& blur = ctx.chain().get<Blur>("blur");
    blur.radius = 4.0f + sin(ctx.time()) * 4.0f;
}

VIVID_CHAIN(setup, update)
