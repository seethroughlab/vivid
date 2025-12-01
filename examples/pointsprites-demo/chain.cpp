// PointSprites Demo - GPU-instanced circle rendering with patterns
#include <vivid/vivid.h>
#include <cmath>

using namespace vivid;

void setup(Chain& chain) {
    // Grid pattern with rainbow colors
    chain.add<PointSprites>("grid")
        .pattern(PointSprites::Grid)
        .count(100)
        .size(0.03f)
        .colorMode(PointSprites::Rainbow)
        .animate(true)
        .animateSpeed(0.5f)
        .clearColor(0.05f, 0.05f, 0.1f);

    // Spiral pattern
    chain.add<PointSprites>("spiral")
        .pattern(PointSprites::Spiral)
        .count(200)
        .size(0.015f)
        .spiralTurns(5.0f)
        .circleRadius(0.4f)
        .colorMode(PointSprites::Gradient)
        .color(1.0f, 0.2f, 0.5f)
        .color2(0.2f, 0.5f, 1.0f)
        .animate(true)
        .animateSpeed(2.0f)
        .clearColor(0.05f, 0.05f, 0.1f);

    // Circle pattern with pulsing
    chain.add<PointSprites>("circle")
        .pattern(PointSprites::Circle)
        .count(32)
        .size(0.04f)
        .circleRadius(0.35f)
        .colorMode(PointSprites::Rainbow)
        .pulseSize(true)
        .pulseSpeed(3.0f)
        .clearColor(0.05f, 0.05f, 0.1f);

    // Random scattered points
    chain.add<PointSprites>("scatter")
        .pattern(PointSprites::Random)
        .count(300)
        .size(0.01f)
        .sizeVariation(0.5f)
        .colorMode(PointSprites::Random_)
        .animate(true)
        .animateSpeed(1.0f)
        .clearColor(0.05f, 0.05f, 0.1f);

    // Composite all patterns together
    chain.add<Composite>("combined")
        .input("grid")
        .blend("spiral", Composite::Add, 0.7f)
        .blend("circle", Composite::Add, 0.8f)
        .blend("scatter", Composite::Add, 0.5f);

    chain.setOutput("combined");
}

void update(Chain& chain, Context& ctx) {
    // Animate spiral turns
    float turns = 3.0f + 2.0f * std::sin(ctx.time() * 0.3f);
    chain.get<PointSprites>("spiral").spiralTurns(turns);

    // Animate circle radius
    float radius = 0.25f + 0.15f * std::sin(ctx.time() * 0.5f);
    chain.get<PointSprites>("circle").circleRadius(radius);

    // Animate scatter count based on time
    int count = 200 + static_cast<int>(100 * std::sin(ctx.time() * 0.2f));
    chain.get<PointSprites>("scatter").count(count);
}

VIVID_CHAIN(setup, update)
