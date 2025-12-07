// PointSprites Demo - Vivid Example
// Demonstrates pattern-based point rendering

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

// Operators (persistent across hot-reloads)
static PointSprites* grid = nullptr;
static PointSprites* spiral = nullptr;
static PointSprites* scatter = nullptr;
static Output* output = nullptr;

// Current demo index
static int currentDemo = 0;
static double lastSwitch = 0.0;

void setup(Context& ctx) {
    // Clean up previous operators if hot-reloading
    delete grid;
    delete spiral;
    delete scatter;
    delete output;
    grid = nullptr;
    spiral = nullptr;
    scatter = nullptr;
    output = nullptr;

    // Create operators
    grid = new PointSprites();
    spiral = new PointSprites();
    scatter = new PointSprites();
    output = new Output();

    // Grid pattern - regular arrangement
    grid->pattern(Pattern::Grid)
        .count(400)
        .size(0.015f)
        .colorMode(PointColorMode::Gradient)
        .color(0.2f, 0.5f, 1.0f, 1.0f)
        .color2(1.0f, 0.3f, 0.5f, 1.0f)
        .animate(true)
        .animateSpeed(1.5f)
        .clearColor(0.02f, 0.02f, 0.05f, 1.0f);

    // Spiral pattern - golden spiral
    spiral->pattern(Pattern::Spiral)
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
    scatter->pattern(Pattern::Random)
        .count(500)
        .size(0.01f)
        .sizeVariation(0.5f)
        .colorMode(PointColorMode::Random)
        .animate(true)
        .animateSpeed(0.8f)
        .clearColor(0.02f, 0.02f, 0.05f, 1.0f);

    // Start with grid
    output->input(grid);
    currentDemo = 0;
    lastSwitch = ctx.time();
}

void update(Context& ctx) {
    double time = ctx.time();

    // Switch demos every 4 seconds
    if (time - lastSwitch > 4.0) {
        currentDemo = (currentDemo + 1) % 3;
        lastSwitch = time;

        switch (currentDemo) {
            case 0:
                output->input(grid);
                break;
            case 1:
                output->input(spiral);
                break;
            case 2:
                output->input(scatter);
                break;
        }
    }

    // Process all (only the active one will render to output)
    grid->process(ctx);
    spiral->process(ctx);
    scatter->process(ctx);
    output->process(ctx);
}

VIVID_CHAIN(setup, update)
