// Color Grading Example
// Demonstrates: HSV, Brightness, Quantize
//
// Shows color correction and stylization workflow

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/video/video.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::video;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Source: video
    auto& source = chain.add<VideoPlayer>("source");
    source.setFile("assets/sword.mp4");
    source.setLoop(true);
    source.play();

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

    // Canvas for layout
    // NOTE: Canvas.drawImage() requires operators to be processed first.
    // Use input() to establish dependencies for correct processing order.
    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(ctx.width(), ctx.height());
    canvas.input(0, "source");
    canvas.input(1, "hsv");
    canvas.input(2, "brightness");
    canvas.input(3, "quantize");

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

    // Draw 2x2 grid
    auto& canvas = chain.get<Canvas>("canvas");
    canvas.clear(0.05f, 0.05f, 0.07f, 1.0f);

    int w = ctx.width();
    int h = ctx.height();
    int halfW = w / 2;
    int halfH = h / 2;
    int pad = 8;

    // Top-left: Original
    auto& source = chain.get<VideoPlayer>("source");
    canvas.drawImage(source, pad, pad, halfW - pad * 2, halfH - pad * 2);

    // Top-right: HSV
    canvas.drawImage(hsv, halfW + pad, pad, halfW - pad * 2, halfH - pad * 2);

    // Bottom-left: Brightness/Contrast
    canvas.drawImage(brightness, pad, halfH + pad, halfW - pad * 2, halfH - pad * 2);

    // Bottom-right: Quantize
    canvas.drawImage(quantize, halfW + pad, halfH + pad, halfW - pad * 2, halfH - pad * 2);

    // Labels with background boxes
    canvas.fillStyle(0.0f, 0.0f, 0.0f, 0.7f);
    canvas.fillRect(pad, pad, 80, 22);
    canvas.fillRect(halfW + pad, pad, 180, 22);
    canvas.fillRect(pad, halfH + pad, 200, 22);
    canvas.fillRect(halfW + pad, halfH + pad, 140, 22);

    canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);
    auto fm = canvas.fontMetrics();
    float textY = pad + fm.ascent;
    float textY2 = halfH + pad + fm.ascent;
    canvas.fillText("Original", pad + 5, textY);

    char hsvLabel[64];
    snprintf(hsvLabel, sizeof(hsvLabel), "HSV: hue=%.2f sat=%.2f",
        static_cast<float>(hsv.hueShift), static_cast<float>(hsv.saturation));
    canvas.fillText(hsvLabel, halfW + pad + 5, textY);

    char brightLabel[64];
    snprintf(brightLabel, sizeof(brightLabel), "Brightness: %.2f Contrast: %.2f",
        static_cast<float>(brightness.brightness), static_cast<float>(brightness.contrast));
    canvas.fillText(brightLabel, pad + 5, textY2);

    char quantLabel[64];
    snprintf(quantLabel, sizeof(quantLabel), "Quantize: %d levels",
        static_cast<int>(quantize.levels));
    canvas.fillText(quantLabel, halfW + pad + 5, textY2);
}

VIVID_CHAIN(setup, update)
