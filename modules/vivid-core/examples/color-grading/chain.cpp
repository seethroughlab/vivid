// Color Grading Example
// Demonstrates: HSV, Brightness, Quantize, Level, ToneMap
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

    // Level - input/output range with gamma
    auto& level = chain.add<Level>("level");
    level.input("source");

    // ToneMap - HDR compression (demonstrates different curves)
    auto& tonemap = chain.add<ToneMap>("tonemap");
    tonemap.input("source");

    // Canvas for layout
    // NOTE: Canvas.drawImage() requires operators to be processed first.
    // Use input() to establish dependencies for correct processing order.
    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(ctx.width(), ctx.height());
    canvas.input(0, "source");
    canvas.input(1, "hsv");
    canvas.input(2, "brightness");
    canvas.input(3, "quantize");
    canvas.input(4, "level");
    canvas.input(5, "tonemap");

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

    // Animate Level
    auto& level = chain.get<Level>("level");
    level.inBlack = 0.05f + std::sin(t * 0.25f) * 0.05f;   // 0 to 0.1
    level.inWhite = 0.95f - std::sin(t * 0.35f) * 0.05f;   // 0.9 to 1.0
    level.gamma = 1.0f + std::sin(t * 0.45f) * 0.5f;       // 0.5 to 1.5

    // Animate ToneMap
    auto& tonemap = chain.get<ToneMap>("tonemap");
    tonemap.exposure = 1.0f + std::sin(t * 0.55f) * 0.5f;  // 0.5 to 1.5
    tonemap.mode = static_cast<int>(std::fmod(t * 0.1f, 3.0f));  // Cycle through modes

    // Draw 3x2 grid
    auto& canvas = chain.get<Canvas>("canvas");
    canvas.clear(0.05f, 0.05f, 0.07f, 1.0f);

    int w = ctx.width();
    int h = ctx.height();
    int thirdW = w / 3;
    int halfH = h / 2;
    int pad = 6;

    // Get operators for drawing
    auto& source = chain.get<VideoPlayer>("source");

    // Top row: Original, HSV, Brightness
    canvas.drawImage(source, pad, pad, thirdW - pad * 2, halfH - pad * 2);
    canvas.drawImage(hsv, thirdW + pad, pad, thirdW - pad * 2, halfH - pad * 2);
    canvas.drawImage(brightness, thirdW * 2 + pad, pad, thirdW - pad * 2, halfH - pad * 2);

    // Bottom row: Quantize, Level, ToneMap
    canvas.drawImage(quantize, pad, halfH + pad, thirdW - pad * 2, halfH - pad * 2);
    canvas.drawImage(level, thirdW + pad, halfH + pad, thirdW - pad * 2, halfH - pad * 2);
    canvas.drawImage(tonemap, thirdW * 2 + pad, halfH + pad, thirdW - pad * 2, halfH - pad * 2);

    // Labels with background boxes
    auto fm = canvas.fontMetrics();
    float textY1 = pad + fm.ascent;
    float textY2 = halfH + pad + fm.ascent;

    // Top row labels
    canvas.fillStyle(0.0f, 0.0f, 0.0f, 0.7f);
    canvas.fillRect(pad, pad, 70, 20);
    canvas.fillRect(thirdW + pad, pad, 140, 20);
    canvas.fillRect(thirdW * 2 + pad, pad, 160, 20);

    canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);
    canvas.fillText("Original", pad + 4, textY1);

    char hsvLabel[64];
    snprintf(hsvLabel, sizeof(hsvLabel), "HSV h=%.2f s=%.2f",
        static_cast<float>(hsv.hueShift), static_cast<float>(hsv.saturation));
    canvas.fillText(hsvLabel, thirdW + pad + 4, textY1);

    char brightLabel[64];
    snprintf(brightLabel, sizeof(brightLabel), "Bright %.2f Con %.2f",
        static_cast<float>(brightness.brightness), static_cast<float>(brightness.contrast));
    canvas.fillText(brightLabel, thirdW * 2 + pad + 4, textY1);

    // Bottom row labels
    canvas.fillStyle(0.0f, 0.0f, 0.0f, 0.7f);
    canvas.fillRect(pad, halfH + pad, 100, 20);
    canvas.fillRect(thirdW + pad, halfH + pad, 130, 20);
    canvas.fillRect(thirdW * 2 + pad, halfH + pad, 140, 20);

    canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);

    char quantLabel[64];
    snprintf(quantLabel, sizeof(quantLabel), "Quantize %d",
        static_cast<int>(quantize.levels));
    canvas.fillText(quantLabel, pad + 4, textY2);

    char levelLabel[64];
    snprintf(levelLabel, sizeof(levelLabel), "Level g=%.2f",
        static_cast<float>(level.gamma));
    canvas.fillText(levelLabel, thirdW + pad + 4, textY2);

    const char* modeNames[] = {"Reinhard", "ACES", "Filmic"};
    char tonemapLabel[64];
    snprintf(tonemapLabel, sizeof(tonemapLabel), "ToneMap %s",
        modeNames[static_cast<int>(tonemap.mode) % 3]);
    canvas.fillText(tonemapLabel, thirdW * 2 + pad + 4, textY2);
}

VIVID_CHAIN(setup, update)
