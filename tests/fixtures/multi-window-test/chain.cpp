// Multi-Window Test
// Demonstrates WindowManager API for secondary windows and span mode

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

int secondaryWindow = -1;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    auto& noise = chain.add<Noise>("noise");
    noise.scale = 4.0f;
    noise.speed = 0.3f;
    noise.octaves = 3;

    chain.output("noise");
}

void update(Context& ctx) {
    // Multi-window example (uncomment for interactive mode):
    // Create secondary window after a few frames
    // if (ctx.frame() == 10 && secondaryWindow < 0) {
    //     secondaryWindow = ctx.createOutputWindow(0);  // Same monitor
    //     if (secondaryWindow >= 0) {
    //         ctx.setOutputWindowSize(secondaryWindow, 400, 300);
    //         ctx.setOutputWindowPos(secondaryWindow, 100, 100);
    //     }
    // }
}

VIVID_CHAIN(setup, update)
