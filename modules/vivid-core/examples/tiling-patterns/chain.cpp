// Tiling Patterns - Vivid Example
// Demonstrates: Tile, Transform, Mirror
//
// Creates repeating patterns and kaleidoscopic effects

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Create an interesting source pattern
    auto& noise = chain.add<Noise>("noise");
    noise.scale = 4.0f;
    noise.octaves = 3;
    noise.type(NoiseType::Simplex);

    auto& shape = chain.add<Shape>("shape");
    shape.type(ShapeType::Star);
    shape.sides = 6;
    shape.size.set(0.3f, 0.3f);
    shape.softness = 0.05f;
    shape.color.set(1.0f, 0.6f, 0.2f, 1.0f);

    auto& source = chain.add<Composite>("source");
    source.inputA("noise");
    source.inputB("shape");
    source.mode(BlendMode::Add);

    // HSV for color variation
    auto& hsv = chain.add<HSV>("hsv");
    hsv.input("source");
    hsv.saturation = 1.3f;

    // ----- TILE EFFECT -----
    // Repeats the image in a grid
    auto& tile_simple = chain.add<Tile>("tile_simple");
    tile_simple.input("hsv");
    tile_simple.repeat.set(3.0f, 3.0f);  // 3x3 grid
    tile_simple.offset.set(0.0f, 0.0f);
    tile_simple.mirror = false;

    // ----- TILE WITH MIRRORING -----
    // Creates seamless patterns by mirroring at boundaries
    auto& tile_mirror = chain.add<Tile>("tile_mirror");
    tile_mirror.input("hsv");
    tile_mirror.repeat.set(3.0f, 3.0f);
    tile_mirror.mirror = true;  // Mirror at tile boundaries

    // ----- TRANSFORM + TILE -----
    // Rotate and scale before tiling for more complex patterns
    auto& transform = chain.add<Transform>("transform");
    transform.input("hsv");
    transform.rotation = 0.785f;  // 45 degrees in radians
    transform.scale.set(0.7f, 0.7f);

    auto& tile_rotated = chain.add<Tile>("tile_rotated");
    tile_rotated.input("transform");
    tile_rotated.repeat.set(4.0f, 4.0f);
    tile_rotated.mirror = true;

    // ----- MIRROR EFFECT -----
    // Kaleidoscope-style mirroring
    auto& mirror = chain.add<Mirror>("mirror");
    mirror.input("hsv");
    mirror.mode(MirrorMode::Quad);  // 4-way symmetry
    mirror.segments = 4;

    // ----- KALEIDOSCOPE (Transform + Mirror) -----
    // Rotate the source, then apply kaleidoscope
    auto& transform_k = chain.add<Transform>("transform_k");
    transform_k.input("hsv");

    auto& kaleidoscope = chain.add<Mirror>("kaleidoscope");
    kaleidoscope.input("transform_k");
    kaleidoscope.mode(MirrorMode::Kaleidoscope);
    kaleidoscope.segments = 8;  // 8-way symmetry

    // Post-process with bloom for polish
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("kaleidoscope");
    bloom.threshold = 0.6f;
    bloom.intensity = 0.8f;
    bloom.radius = 12.0f;

    // Canvas for comparison
    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(ctx.width(), ctx.height());
    canvas.input(0, "tile_simple");
    canvas.input(1, "tile_mirror");
    canvas.input(2, "tile_rotated");
    canvas.input(3, "bloom");

    chain.output("canvas");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    // Animate source
    auto& noise = chain.get<Noise>("noise");
    noise.scale = 3.0f + std::sin(t * 0.2f) * 1.5f;

    auto& shape = chain.get<Shape>("shape");
    shape.rotation = t * 0.3f;
    float pulse = 0.25f + 0.1f * std::sin(t * 1.5f);
    shape.size.set(pulse, pulse);

    auto& hsv = chain.get<HSV>("hsv");
    hsv.hueShift = std::fmod(t * 0.05f, 1.0f);

    // Mouse controls
    float mouseX = ctx.mouseNorm().x * 0.5f + 0.5f;  // 0-1
    float mouseY = ctx.mouseNorm().y * 0.5f + 0.5f;  // 0-1

    // X: tile repeat count (1-6)
    float repeat = 1.0f + mouseX * 5.0f;

    // Y: offset animation speed
    float offsetSpeed = mouseY * 2.0f;

    // Animate tile offset for scrolling effect
    float offsetX = std::fmod(t * offsetSpeed * 0.1f, 1.0f);
    float offsetY = std::fmod(t * offsetSpeed * 0.07f, 1.0f);

    auto& tile_simple = chain.get<Tile>("tile_simple");
    tile_simple.repeat.set(repeat, repeat);
    tile_simple.offset.set(offsetX, offsetY);

    auto& tile_mirror = chain.get<Tile>("tile_mirror");
    tile_mirror.repeat.set(repeat, repeat);
    tile_mirror.offset.set(offsetX * 0.5f, offsetY * 0.5f);

    auto& tile_rotated = chain.get<Tile>("tile_rotated");
    tile_rotated.repeat.set(repeat + 1.0f, repeat + 1.0f);

    // Animate transform rotation
    auto& transform = chain.get<Transform>("transform");
    transform.rotation = t * 0.1f;

    // Animate kaleidoscope rotation
    auto& transform_k = chain.get<Transform>("transform_k");
    transform_k.rotation = t * 0.15f;

    // Animate kaleidoscope segments (4-12)
    auto& kaleidoscope = chain.get<Mirror>("kaleidoscope");
    int segments = 4 + static_cast<int>(std::sin(t * 0.2f) * 4.0f + 4.0f);
    kaleidoscope.segments = segments;

    // Draw 2x2 grid
    auto& canvas = chain.get<Canvas>("canvas");
    canvas.clear(0.02f, 0.02f, 0.04f, 1.0f);

    int w = ctx.width();
    int h = ctx.height();
    int halfW = w / 2;
    int halfH = h / 2;
    int pad = 8;

    canvas.drawImage(tile_simple, pad, pad, halfW - pad * 2, halfH - pad * 2);
    canvas.drawImage(tile_mirror, halfW + pad, pad, halfW - pad * 2, halfH - pad * 2);
    canvas.drawImage(tile_rotated, pad, halfH + pad, halfW - pad * 2, halfH - pad * 2);

    auto& bloom = chain.get<Bloom>("bloom");
    canvas.drawImage(bloom, halfW + pad, halfH + pad, halfW - pad * 2, halfH - pad * 2);

    // Labels
    canvas.fillStyle(0.0f, 0.0f, 0.0f, 0.7f);
    canvas.fillRect(pad, pad, 180, 22);
    canvas.fillRect(halfW + pad, pad, 180, 22);
    canvas.fillRect(pad, halfH + pad, 160, 22);
    canvas.fillRect(halfW + pad, halfH + pad, 200, 22);

    canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);

    char label1[64];
    snprintf(label1, sizeof(label1), "Tile: %.1fx%.1f (no mirror)", repeat, repeat);
    canvas.fillText(label1, pad + 5, pad + 16);

    char label2[64];
    snprintf(label2, sizeof(label2), "Tile: %.1fx%.1f (mirror)", repeat, repeat);
    canvas.fillText(label2, halfW + pad + 5, pad + 16);

    canvas.fillText("Transform + Tile", pad + 5, halfH + pad + 16);

    char label4[64];
    snprintf(label4, sizeof(label4), "Kaleidoscope: %d segments", segments);
    canvas.fillText(label4, halfW + pad + 5, halfH + pad + 16);
}

VIVID_CHAIN(setup, update)
