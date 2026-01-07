// Image Pipeline Example
// Demonstrates: Image, Transform, Mirror, Tile
//
// Shows a 2x2 grid with progressive transformations applied to an image

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Fallback: generate a colorful noise pattern (always works)
    auto& source = chain.add<Noise>("source");
    source.scale = 3.0f;
    source.octaves = 4;

    // Transform - scale, rotate, translate
    auto& transform = chain.add<Transform>("transform");
    transform.input("source");
    transform.scale.set(0.8f, 0.8f);

    // Mirror - kaleidoscope mode
    auto& mirror = chain.add<Mirror>("mirror");
    mirror.input("transform");
    mirror.mode(MirrorMode::Kaleidoscope);
    mirror.segments = 6;

    // Tile - repeat the texture
    auto& tile = chain.add<Tile>("tile");
    tile.input("transform");
    tile.repeat.set(3.0f, 3.0f);

    // Combined: Mirror + Tile
    auto& combined = chain.add<Tile>("combined");
    combined.input("mirror");
    combined.repeat.set(2.0f, 2.0f);

    // Canvas for grid layout
    // NOTE: Canvas.drawImage() requires operators to be processed first.
    // Use input() to establish dependencies for correct processing order.
    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(ctx.width(), ctx.height());
    canvas.input(0, "transform");
    canvas.input(1, "mirror");
    canvas.input(2, "tile");
    canvas.input(3, "combined");

    chain.output("canvas");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    // Animate transforms
    auto& transform = chain.get<Transform>("transform");
    transform.rotation = std::sin(t * 0.5f) * 0.5f;  // radians
    transform.translate.set(
        std::sin(t * 0.3f) * 0.1f,
        std::cos(t * 0.4f) * 0.1f
    );

    // Animate mirror segments
    auto& mirror = chain.get<Mirror>("mirror");
    int segments = 4 + static_cast<int>(std::sin(t * 0.2f) * 2);
    mirror.segments = segments;
    mirror.angle = t * 0.1f;

    // Animate tile count
    auto& tile = chain.get<Tile>("tile");
    float repeatX = 2.0f + std::abs(std::sin(t * 0.3f)) * 3.0f;
    float repeatY = 2.0f + std::abs(std::cos(t * 0.3f)) * 3.0f;
    tile.repeat.set(repeatX, repeatY);

    // Draw 2x2 grid
    auto& canvas = chain.get<Canvas>("canvas");
    canvas.clear(0.08f, 0.08f, 0.1f, 1.0f);

    int w = ctx.width();
    int h = ctx.height();
    int halfW = w / 2;
    int halfH = h / 2;
    int pad = 10;

    auto& combined = chain.get<Tile>("combined");

    // Top-left: Original/Transformed
    canvas.drawImage(transform, pad, pad, halfW - pad * 2, halfH - pad * 2);

    // Top-right: Mirror/Kaleidoscope
    canvas.drawImage(mirror, halfW + pad, pad, halfW - pad * 2, halfH - pad * 2);

    // Bottom-left: Tile
    canvas.drawImage(tile, pad, halfH + pad, halfW - pad * 2, halfH - pad * 2);

    // Bottom-right: Combined (Mirror + Tile)
    canvas.drawImage(combined, halfW + pad, halfH + pad, halfW - pad * 2, halfH - pad * 2);

    // Labels
    canvas.fillStyle(1.0f, 1.0f, 1.0f, 0.9f);
    canvas.fillText("Transform (scale, rotate, translate)", pad + 5, pad + 20);

    char mirrorLabel[64];
    snprintf(mirrorLabel, sizeof(mirrorLabel), "Mirror (kaleidoscope: %d segments)", segments);
    canvas.fillText(mirrorLabel, halfW + pad + 5, pad + 20);

    char tileLabel[32];
    snprintf(tileLabel, sizeof(tileLabel), "Tile (%.0fx%.0f)", repeatX, repeatY);
    canvas.fillText(tileLabel, pad + 5, halfH + pad + 20);

    canvas.fillText("Combined: Mirror + Tile", halfW + pad + 5, halfH + pad + 20);
}

VIVID_CHAIN(setup, update)
