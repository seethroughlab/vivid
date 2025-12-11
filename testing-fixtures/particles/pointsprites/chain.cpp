// PointSprites Demo - Vivid Example
// Demonstrates pattern-based point rendering

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

// Current demo index
static int currentDemo = 0;
static double lastSwitch = 0.0;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Grid pattern - regular arrangement
    chain.add<PointSprites>("grid")
        .pattern(Pattern::Grid)
        .count(400)
        .size(0.015f)
        .colorMode(PointColorMode::Gradient)
        .color(0.2f, 0.5f, 1.0f, 1.0f)
        .color2(1.0f, 0.3f, 0.5f, 1.0f)
        .animate(true)
        .animateSpeed(1.5f)
        .clearColor(0.02f, 0.02f, 0.05f, 1.0f);

    // Spiral pattern - golden spiral
    chain.add<PointSprites>("spiral")
        .pattern(Pattern::Spiral)
        .count(300)
        .size(0.012f)
        .sizeVariation(0.3f)
        .colorMode(PointColorMode::Rainbow)
        .circleRadius(0.4f)
        .spiralTurns(5.0f)
        .pulseSize(true)
        .pulseSpeed(3.0f)
        .clearColor(0.02f, 0.02f, 0.05f, 1.0f);

    // Random scatter - chaotic points
    chain.add<PointSprites>("scatter")
        .pattern(Pattern::Random)
        .count(500)
        .size(0.01f)
        .sizeVariation(0.5f)
        .colorMode(PointColorMode::Random)
        .animate(true)
        .animateSpeed(0.8f)
        .clearColor(0.02f, 0.02f, 0.05f, 1.0f);

    // Start with grid
    chain.output("grid");
    currentDemo = 0;
    lastSwitch = ctx.time();
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    double time = ctx.time();

    // Switch demos every 4 seconds
    if (time - lastSwitch > 4.0) {
        currentDemo = (currentDemo + 1) % 3;
        lastSwitch = time;

        switch (currentDemo) {
            case 0: chain.output("grid"); break;
            case 1: chain.output("spiral"); break;
            case 2: chain.output("scatter"); break;
        }
    }
}

VIVID_CHAIN(setup, update)
