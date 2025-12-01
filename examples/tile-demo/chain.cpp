// Tile Demo - Texture tiling with per-tile transforms
#include <vivid/vivid.h>
#include <cmath>

using namespace vivid;

void setup(Chain& chain) {
    // Create a source pattern using noise
    chain.add<Noise>("source")
        .scale(2.0f)
        .speed(0.5f)
        .octaves(3);

    // Colorize the noise
    chain.add<HSV>("colored")
        .input("source")
        .saturation(1.2f)
        .value(1.0f);

    // Simple 4x4 tile grid
    chain.add<Tile>("basic_grid")
        .input("colored")
        .cols(4)
        .rows(4)
        .gap(0.02f);

    // Brick pattern with offset rows
    chain.add<Tile>("bricks")
        .input("colored")
        .cols(6)
        .rows(4)
        .gap(0.01f)
        .offsetOddRows(0.5f)
        .scalePerTile(0.9f);

    // Alternating mirror pattern
    chain.add<Tile>("mirror")
        .input("colored")
        .cols(3)
        .rows(3)
        .mirrorAlternate(true);

    // Rotating tiles
    chain.add<Tile>("rotating")
        .input("colored")
        .cols(4)
        .rows(4)
        .gap(0.03f)
        .animateRotation(true)
        .animateSpeed(0.5f)
        .scalePerTile(0.8f);

    // Composite two patterns
    chain.add<Composite>("combined")
        .input("basic_grid")
        .blend("rotating", Composite::Multiply, 1.0f);

    chain.setOutput("combined");
}

void update(Chain& chain, Context& ctx) {
    // Animate hue shift on the source
    float hue = std::fmod(ctx.time() * 0.1f, 1.0f);
    chain.get<HSV>("colored").hueShift(hue);

    // Animate tile scale
    float scale = 0.7f + 0.3f * std::sin(ctx.time() * 0.8f);
    chain.get<Tile>("basic_grid").scalePerTile(scale);

    // Cycle through different outputs
    int mode = static_cast<int>(ctx.time() / 4.0f) % 4;
    switch (mode) {
        case 0: chain.setOutput("basic_grid"); break;
        case 1: chain.setOutput("bricks"); break;
        case 2: chain.setOutput("mirror"); break;
        case 3: chain.setOutput("rotating"); break;
    }
}

VIVID_CHAIN(setup, update)
