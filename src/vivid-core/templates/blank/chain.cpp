// %PROJECT_NAME% - Vivid Project
// Edit this file and save to see live changes!

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Add your operators here
    auto& noise = chain.add<Noise>("noise");
    noise.scale = 4.0f;
    noise.speed = 0.5f;

    chain.output("noise");
}

void update(Context& ctx) {
    // Animate parameters here
    auto& chain = ctx.chain();
    float time = static_cast<float>(ctx.time());

    // Example: animate noise offset
    chain.get<Noise>("noise").offset.set(time * 0.2f, time * 0.1f, 0.0f);
}

VIVID_CHAIN(setup, update)
