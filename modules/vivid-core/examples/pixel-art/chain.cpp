// Pixel Art - Vivid Example
// Demonstrates: Pixelate, Quantize, Image
//
// Creates a pixel art aesthetic with chunky pixels and limited color palette

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Load sprite image - pixel art effects work great on detailed images
    auto& source = chain.add<Image>("source");
    source.file = "assets/sprite.png";

    // ----- PIXELATE EFFECT -----
    // Creates blocky mosaic pixels - different from Downsample
    // Downsample reduces resolution then upscales (blurry)
    // Pixelate samples in blocks (sharp edges)
    auto& pixelate = chain.add<Pixelate>("pixelate");
    pixelate.input("source");
    pixelate.size.set(8.0f, 8.0f);  // 8x8 pixel blocks

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
    pixelate_then_quantize.size.set(4.0f, 4.0f);

    auto& pixel_art = chain.add<Quantize>("pixel_art");
    pixel_art.input("pixelate2");
    pixel_art.levels = 8;  // 512 colors

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

    // Animate pixel size for visual interest
    float baseSize = 6.0f + std::sin(t * 0.3f) * 2.0f;

    auto& pixelate = chain.get<Pixelate>("pixelate");
    pixelate.size.set(baseSize, baseSize);

    // Animate color levels
    int levels = 6 + static_cast<int>(std::sin(t * 0.2f) * 2.0f);

    auto& quantize = chain.get<Quantize>("quantize");
    quantize.levels = levels;

    // Combined effect uses fixed moderate settings
    auto& pixelate2 = chain.get<Pixelate>("pixelate2");
    pixelate2.size.set(4.0f, 4.0f);

    auto& pixel_art = chain.get<Quantize>("pixel_art");
    pixel_art.levels = 8;

    // Draw 2x2 comparison grid
    auto& canvas = chain.get<Canvas>("canvas");
    canvas.clear(0.08f, 0.08f, 0.1f, 1.0f);

    int w = ctx.width();
    int h = ctx.height();
    int halfW = w / 2;
    int halfH = h / 2;
    int pad = 10;
    int labelH = 28;

    auto& source = chain.get<Image>("source");

    // Draw images first, then labels on top
    canvas.drawImage(source, pad, pad + labelH, halfW - pad * 2, halfH - pad * 2 - labelH);
    canvas.drawImage(pixelate, halfW + pad, pad + labelH, halfW - pad * 2, halfH - pad * 2 - labelH);
    canvas.drawImage(quantize, pad, halfH + pad + labelH, halfW - pad * 2, halfH - pad * 2 - labelH);
    canvas.drawImage(pixel_art, halfW + pad, halfH + pad + labelH, halfW - pad * 2, halfH - pad * 2 - labelH);

    // Labels with dark backgrounds on top
    canvas.fillStyle(0.0f, 0.0f, 0.0f, 0.85f);
    canvas.fillRect(pad, pad, halfW - pad * 2, labelH);
    canvas.fillRect(halfW + pad, pad, halfW - pad * 2, labelH);
    canvas.fillRect(pad, halfH + pad, halfW - pad * 2, labelH);
    canvas.fillRect(halfW + pad, halfH + pad, halfW - pad * 2, labelH);

    canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);
    canvas.fillText("ORIGINAL", pad + 8, pad + 20);

    char pixLabel[64];
    snprintf(pixLabel, sizeof(pixLabel), "PIXELATE  size=%.0f", baseSize);
    canvas.fillText(pixLabel, halfW + pad + 8, pad + 20);

    char quantLabel[64];
    snprintf(quantLabel, sizeof(quantLabel), "QUANTIZE  levels=%d", levels);
    canvas.fillText(quantLabel, pad + 8, halfH + pad + 20);

    canvas.fillText("PIXEL ART  4px + 8 levels", halfW + pad + 8, halfH + pad + 20);
}

VIVID_CHAIN(setup, update)
