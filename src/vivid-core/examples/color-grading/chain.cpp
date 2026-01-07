// Color Grading Example
// Demonstrates: HSV, Brightness, Quantize
//
// Shows color correction and stylization workflow

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Source: animated gradient
    auto& source = chain.add<Ramp>("source");
    source.hueSpeed = 0.1f;

    // HSV adjustment - hue shift, saturation, value
    auto& hsv = chain.add<HSV>("hsv");
    hsv.input("source");

    // Brightness/Contrast
    auto& brightness = chain.add<Brightness>("brightness");
    brightness.input("source");

    // Quantize - reduce color palette
    auto& quantize = chain.add<Quantize>("quantize");
    quantize.input("source");
    quantize.levels = 8;

    // Combined: HSV -> Brightness -> Quantize
    auto& combined = chain.add<HSV>("combined_hsv");
    combined.input("source");

    auto& combined_bright = chain.add<Brightness>("combined_bright");
    combined_bright.input("combined_hsv");

    auto& combined_quant = chain.add<Quantize>("combined_quant");
    combined_quant.input("combined_bright");

    // Canvas for layout
    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(ctx.width(), ctx.height());

    chain.output("canvas");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    // Animate HSV
    auto& hsv = chain.get<HSV>("hsv");
    hsv.hueShift = std::sin(t * 0.3f) * 0.5f + 0.5f;  // 0 to 1
    hsv.saturation = 0.8f + std::sin(t * 0.5f) * 0.4f;  // 0.4 to 1.2
    hsv.value = 1.0f;

    // Animate Brightness
    auto& brightness = chain.get<Brightness>("brightness");
    brightness.brightness = std::sin(t * 0.4f) * 0.3f;  // -0.3 to 0.3
    brightness.contrast = 1.0f + std::sin(t * 0.6f) * 0.5f;  // 0.5 to 1.5

    // Animate Quantize
    auto& quantize = chain.get<Quantize>("quantize");
    quantize.levels = 4 + static_cast<int>(std::abs(std::sin(t * 0.2f)) * 12);  // 4 to 16 levels

    // Combined chain settings
    auto& combined_hsv = chain.get<HSV>("combined_hsv");
    combined_hsv.hueShift = 0.15f;  // Warm shift
    combined_hsv.saturation = 1.2f;

    auto& combined_bright = chain.get<Brightness>("combined_bright");
    combined_bright.contrast = 1.3f;

    auto& combined_quant = chain.get<Quantize>("combined_quant");
    combined_quant.levels = 6;

    // Draw 2x2 grid
    auto& canvas = chain.get<Canvas>("canvas");
    canvas.clear(0.05f, 0.05f, 0.07f, 1.0f);

    int w = ctx.width();
    int h = ctx.height();
    int halfW = w / 2;
    int halfH = h / 2;
    int pad = 8;

    auto& source = chain.get<Ramp>("source");

    // Top-left: Original
    canvas.drawImage(source, pad, pad, halfW - pad * 2, halfH - pad * 2);

    // Top-right: HSV
    canvas.drawImage(hsv, halfW + pad, pad, halfW - pad * 2, halfH - pad * 2);

    // Bottom-left: Brightness/Contrast
    canvas.drawImage(brightness, pad, halfH + pad, halfW - pad * 2, halfH - pad * 2);

    // Bottom-right: Quantize (or combined)
    canvas.drawImage(combined_quant, halfW + pad, halfH + pad, halfW - pad * 2, halfH - pad * 2);

    // Labels with background boxes
    canvas.fillStyle(0.0f, 0.0f, 0.0f, 0.7f);
    canvas.fillRect(pad, pad, 80, 22);
    canvas.fillRect(halfW + pad, pad, 180, 22);
    canvas.fillRect(pad, halfH + pad, 200, 22);
    canvas.fillRect(halfW + pad, halfH + pad, 180, 22);

    canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);
    canvas.fillText("Original", pad + 5, pad + 16);

    char hsvLabel[64];
    snprintf(hsvLabel, sizeof(hsvLabel), "HSV: hue=%.2f sat=%.2f",
        static_cast<float>(hsv.hueShift), static_cast<float>(hsv.saturation));
    canvas.fillText(hsvLabel, halfW + pad + 5, pad + 16);

    char brightLabel[64];
    snprintf(brightLabel, sizeof(brightLabel), "Brightness: %.2f Contrast: %.2f",
        static_cast<float>(brightness.brightness), static_cast<float>(brightness.contrast));
    canvas.fillText(brightLabel, pad + 5, halfH + pad + 16);

    canvas.fillText("Combined: HSV + Bright + Quantize", halfW + pad + 5, halfH + pad + 16);
}

VIVID_CHAIN(setup, update)
