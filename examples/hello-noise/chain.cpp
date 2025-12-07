// Hello Noise - Vivid Example
// Demonstrates the basic Noise → Output chain

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

// Chain (persistent across hot-reloads)
static Chain* chain = nullptr;

void setup(Context& ctx) {
    // Clean up previous chain if hot-reloading
    delete chain;
    chain = nullptr;

    // Create chain
    chain = new Chain();

    // Configure noise generator
    auto& noise = chain->add<Noise>("noise");
    noise.scale(4.0f)
        .speed(0.5f)
        .octaves(4)
        .lacunarity(2.0f)
        .persistence(0.5f);

    // Connect to output
    chain->add<Output>("output").input(&noise);
    chain->setOutput("output");
    chain->init(ctx);

    if (chain->hasError()) {
        ctx.setError(chain->error());
    }
}

void update(Context& ctx) {
    if (!chain) return;

    // Process the chain
    chain->process(ctx);
}

VIVID_CHAIN(setup, update)
