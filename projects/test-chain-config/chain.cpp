// Test chain for VIVID_CHAIN_CONFIG
#include <vivid/vivid.h>
#include <vivid/effects/noise.h>
#include <iostream>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    std::cout << "Setup: ctx.width()=" << ctx.width() << ", ctx.height()=" << ctx.height() << std::endl;

    // Add a simple noise generator - should use window size (800x600)
    chain.add<Noise>("noise");

    // Set as output by name
    chain.output("noise");
}

void update(Context& ctx) {
    ctx.chain().process(ctx);
}

// Test the new VIVID_CHAIN_CONFIG macro
VIVID_CHAIN_CONFIG(setup, update, (vivid::ChainConfig{
    .windowWidth = 800,
    .windowHeight = 600,
    .resizable = false,
    .fullscreen = false
}))
