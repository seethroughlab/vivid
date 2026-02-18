// Test fixture: chain with VIVID_CHAIN_CONFIG for hot-reload tests
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();
    chain.add<Noise>("noise");
    chain.output("noise");
}
void update(Context& ctx) {}
VIVID_CHAIN_CONFIG(setup, update, (vivid::ChainConfig{
    .windowWidth = 1920,
    .windowHeight = 1080
}))
