// Hello Noise - Vivid Example
// Demonstrates the basic Noise → Output chain
// Shows how to set explicit resolution on generators

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
    noise.resolution(1920, 1080)  // Set to 1080p
        .scale(4.0f)
        .speed(0.5f)
        .octaves(4)
        .lacunarity(2.0f)
        .persistence(0.5f);

    // Specify output - will be scaled to window size for display
    chain.output("noise");
}

void update(Context& ctx) {
    // No parameter tweaks needed - noise animates via time
}

VIVID_CHAIN(setup, update)
