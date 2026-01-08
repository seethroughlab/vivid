// Pixel Art - Vivid Example
// Demonstrates: Pixelate, Quantize
//
// Creates a pixel art aesthetic with chunky pixels and limited color palette

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Create a vibrant source with smooth gradients
    // The pixel art effect looks best on colorful, high-detail sources
    auto& gradient = chain.add<Gradient>("gradient");
    gradient.mode(GradientMode::Radial);
    gradient.colorA.set(1.0f, 0.4f, 0.1f, 1.0f);   // Warm orange center
    gradient.colorB.set(0.1f, 0.2f, 0.8f, 1.0f);   // Cool blue edge

    // Animated shape for visual interest
    auto& shape = chain.add<Shape>("shape");
    shape.type(ShapeType::Star);
    shape.sides = 6;
    shape.size.set(0.35f, 0.35f);
    shape.softness = 0.1f;
    shape.color.set(0.2f, 1.0f, 0.4f, 1.0f);  // Bright green

    auto& shape2 = chain.add<Shape>("shape2");
    shape2.type(ShapeType::Circle);
    shape2.size.set(0.15f, 0.15f);
    shape2.position.set(-0.3f, 0.2f);
    shape2.color.set(1.0f, 0.2f, 0.6f, 1.0f);  // Magenta

    // Composite shapes onto gradient
    auto& comp1 = chain.add<Composite>("comp1");
    comp1.inputA("gradient");
    comp1.inputB("shape");
    comp1.mode(BlendMode::Add);

    auto& source = chain.add<Composite>("source");
    source.inputA("comp1");
    source.inputB("shape2");
    source.mode(BlendMode::Add);

    // ----- PIXELATE EFFECT -----
    // Creates blocky mosaic pixels - different from Downsample
    // Downsample reduces resolution then upscales (blurry)
    // Pixelate samples in blocks (sharp edges)
    auto& pixelate = chain.add<Pixelate>("pixelate");
    pixelate.input("source");
    pixelate.size.set(12.0f, 12.0f);  // 12x12 pixel blocks

    // ----- QUANTIZE EFFECT -----
    // Reduces colors per channel (posterization)
    // 4 levels = 4^3 = 64 total colors
    auto& quantize = chain.add<Quantize>("quantize");
    quantize.input("source");
    quantize.levels = 8;  // 8 levels = 512 colors

    // ----- COMBINED: Pixelate + Quantize -----
    // Classic pixel art: blocky pixels AND limited palette
    auto& pixelate_then_quantize = chain.add<Pixelate>("pixelate2");
    pixelate_then_quantize.input("source");
    pixelate_then_quantize.size.set(8.0f, 8.0f);

    auto& pixel_art = chain.add<Quantize>("pixel_art");
    pixel_art.input("pixelate2");
    pixel_art.levels = 4;  // 64 colors - true retro feel

    // Canvas for 2x2 comparison grid
    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(ctx.width(), ctx.height());
    canvas.input(0, "source");
    canvas.input(1, "pixelate");
    canvas.input(2, "quantize");
    canvas.input(3, "pixel_art");

    chain.output("canvas");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    // Animate shapes
    auto& shape = chain.get<Shape>("shape");
    shape.rotation = t * 0.3f;
    float pulse = 0.3f + 0.1f * std::sin(t * 1.5f);
    shape.size.set(pulse, pulse);

    auto& shape2 = chain.get<Shape>("shape2");
    shape2.position.set(-0.3f + std::sin(t * 0.7f) * 0.15f,
                        0.2f + std::cos(t * 0.9f) * 0.15f);

    // Animate gradient hue via colors
    float hueShift = std::fmod(t * 0.1f, 1.0f);
    auto& gradient = chain.get<Gradient>("gradient");
    // Shift warm color through spectrum
    gradient.colorA.set(
        0.5f + 0.5f * std::sin(hueShift * 6.28f),
        0.5f + 0.5f * std::sin(hueShift * 6.28f + 2.09f),
        0.5f + 0.5f * std::sin(hueShift * 6.28f + 4.19f),
        1.0f
    );

    // Mouse controls pixelation amount
    // X axis: pixel size (4-32)
    float mouseX = ctx.mouseNorm().x * 0.5f + 0.5f;  // 0-1
    float pixelSize = 4.0f + mouseX * 28.0f;

    auto& pixelate = chain.get<Pixelate>("pixelate");
    pixelate.size.set(pixelSize, pixelSize);

    // Y axis: color levels (2-16)
    float mouseY = ctx.mouseNorm().y * 0.5f + 0.5f;  // 0-1
    int levels = 2 + static_cast<int>(mouseY * 14.0f);

    auto& quantize = chain.get<Quantize>("quantize");
    quantize.levels = levels;

    // Combined effect uses moderate settings
    auto& pixelate2 = chain.get<Pixelate>("pixelate2");
    pixelate2.size.set(8.0f + std::sin(t * 0.5f) * 4.0f,
                       8.0f + std::sin(t * 0.5f) * 4.0f);

    auto& pixel_art = chain.get<Quantize>("pixel_art");
    pixel_art.levels = 4 + static_cast<int>(std::sin(t * 0.3f) * 2.0f);

    // Draw 2x2 comparison grid
    auto& canvas = chain.get<Canvas>("canvas");
    canvas.clear(0.05f, 0.05f, 0.08f, 1.0f);

    int w = ctx.width();
    int h = ctx.height();
    int halfW = w / 2;
    int halfH = h / 2;
    int pad = 8;

    auto& source = chain.get<Composite>("source");

    // Top-left: Original
    canvas.drawImage(source, pad, pad, halfW - pad * 2, halfH - pad * 2);

    // Top-right: Pixelate only
    canvas.drawImage(pixelate, halfW + pad, pad, halfW - pad * 2, halfH - pad * 2);

    // Bottom-left: Quantize only
    canvas.drawImage(quantize, pad, halfH + pad, halfW - pad * 2, halfH - pad * 2);

    // Bottom-right: Pixelate + Quantize (pixel art)
    canvas.drawImage(pixel_art, halfW + pad, halfH + pad, halfW - pad * 2, halfH - pad * 2);

    // Labels with dark backgrounds
    canvas.fillStyle(0.0f, 0.0f, 0.0f, 0.7f);
    canvas.fillRect(pad, pad, 70, 22);
    canvas.fillRect(halfW + pad, pad, 200, 22);
    canvas.fillRect(pad, halfH + pad, 200, 22);
    canvas.fillRect(halfW + pad, halfH + pad, 220, 22);

    canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);
    canvas.fillText("Original", pad + 5, pad + 16);

    char pixLabel[64];
    snprintf(pixLabel, sizeof(pixLabel), "Pixelate: size=%.0f", pixelSize);
    canvas.fillText(pixLabel, halfW + pad + 5, pad + 16);

    char quantLabel[64];
    snprintf(quantLabel, sizeof(quantLabel), "Quantize: levels=%d (%d colors)", levels, levels * levels * levels);
    canvas.fillText(quantLabel, pad + 5, halfH + pad + 16);

    canvas.fillText("Pixel Art: Pixelate + Quantize", halfW + pad + 5, halfH + pad + 16);
}

VIVID_CHAIN(setup, update)
