// Hello Noise - Vivid Example
// Demonstrates the basic Noise → Output chain

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Configure noise generator
    auto& noise = chain.add<Noise>("noise");
    noise.scale(4.0f)
        .speed(0.5f)
        .octaves(4)
        .lacunarity(2.0f)
        .persistence(0.5f);

    // Specify output
    chain.output("noise");
}

void update(Context& ctx) {
    // No parameter tweaks needed - noise animates via time
}

VIVID_CHAIN(setup, update)
