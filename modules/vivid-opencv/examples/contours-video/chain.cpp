// Contours - Video
// Contour detection on video file using OpenCV
//
// Shows original video overlaid with detected contours.
// Demonstrates blending contours with source footage.
//
// Controls:
//   Mouse X: Canny threshold 1
//   Mouse Y: Canny threshold 2
//   Space: Pause/play video
//   B: Toggle contour/blend mode
//   C: Cycle contour colors

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/video/video.h>
#include <vivid/opencv/opencv.h>
#include <cmath>
#include <iostream>

using namespace vivid;
using namespace vivid::effects;

static bool blendMode = true;  // true = overlay on video, false = contours only
static int colorPreset = 0;
static bool paused = false;

// Color presets for contours
static const float colors[][4] = {
    {0.0f, 1.0f, 0.0f, 1.0f},   // Green
    {1.0f, 0.0f, 0.0f, 1.0f},   // Red
    {0.0f, 0.5f, 1.0f, 1.0f},   // Cyan
    {1.0f, 1.0f, 0.0f, 1.0f},   // Yellow
    {1.0f, 0.0f, 1.0f, 1.0f},   // Magenta
    {1.0f, 1.0f, 1.0f, 1.0f},   // White
};
static const int numColors = 6;

void printControls() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Contours - Video" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  Mouse X: Canny threshold 1 (0-255)" << std::endl;
    std::cout << "  Mouse Y: Canny threshold 2 (0-255)" << std::endl;
    std::cout << "  Space: Pause/play video" << std::endl;
    std::cout << "  B: Toggle blend mode" << std::endl;
    std::cout << "  C: Cycle contour colors" << std::endl;
    std::cout << "========================================\n" << std::endl;
}

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    printControls();

    // =========================================================================
    // Video Source
    // =========================================================================

    auto& video = chain.add<vivid::video::VideoPlayer>("video");
    video.setFile("assets/train.mp4");
    video.setLoop(true);
    video.play();

    // =========================================================================
    // OpenCV Contour Detection
    // =========================================================================

    auto& contours = chain.add<vivid::opencv::Contours>("contours");
    contours.input("video");
    contours.threshold1 = 50.0f;
    contours.threshold2 = 150.0f;
    contours.lineWidth = 2.0f;
    contours.colorR = colors[colorPreset][0];
    contours.colorG = colors[colorPreset][1];
    contours.colorB = colors[colorPreset][2];
    contours.colorA = colors[colorPreset][3];

    // =========================================================================
    // Compositing - Overlay contours on video
    // =========================================================================

    // Additive blend: contours glow on top of video
    auto& composite = chain.add<Composite>("composite");
    composite.inputA("video");
    composite.inputB("contours");
    composite.mode = BlendMode::Add;

    // =========================================================================
    // Output Canvas with Labels
    // =========================================================================

    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(ctx.width(), ctx.height());
    canvas.input(0, "composite");
    canvas.input(1, "contours");

    chain.output("canvas");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // Input Handling
    // =========================================================================

    // Space: Pause/play
    if (ctx.key(GLFW_KEY_SPACE).pressed) {
        paused = !paused;
        auto& video = chain.get<vivid::video::VideoPlayer>("video");
        if (paused) {
            video.pause();
        } else {
            video.play();
        }
    }

    // B: Toggle blend mode
    if (ctx.key(GLFW_KEY_B).pressed) {
        blendMode = !blendMode;
    }

    // C: Cycle colors
    if (ctx.key(GLFW_KEY_C).pressed) {
        colorPreset = (colorPreset + 1) % numColors;
        auto& contours = chain.get<vivid::opencv::Contours>("contours");
        contours.colorR = colors[colorPreset][0];
        contours.colorG = colors[colorPreset][1];
        contours.colorB = colors[colorPreset][2];
        contours.colorA = colors[colorPreset][3];
    }

    // =========================================================================
    // Mouse Controls - Canny Thresholds
    // =========================================================================

    glm::vec2 mouse = ctx.mouseNorm();
    float threshold1 = mouse.x * 255.0f;
    float threshold2 = mouse.y * 255.0f;

    auto& contours = chain.get<vivid::opencv::Contours>("contours");
    contours.threshold1 = threshold1;
    contours.threshold2 = threshold2;

    // =========================================================================
    // Draw Output
    // =========================================================================

    auto& canvas = chain.get<Canvas>("canvas");
    canvas.clear(0.0f, 0.0f, 0.0f, 1.0f);

    int w = ctx.width();
    int h = ctx.height();
    int pad = 10;
    int labelH = 28;

    // Draw main view (blended or contours only)
    if (blendMode) {
        auto& composite = chain.get<Composite>("composite");
        canvas.drawImage(composite, 0, labelH, w, h - labelH);
    } else {
        canvas.drawImage(contours, 0, labelH, w, h - labelH);
    }

    // Label bar
    canvas.fillStyle(0.0f, 0.0f, 0.0f, 0.85f);
    canvas.fillRect(0, 0, w, labelH);

    canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);
    auto fm = canvas.fontMetrics();
    float textY = (labelH + fm.ascent - fm.descent) * 0.5f;

    char label[128];
    snprintf(label, sizeof(label),
             "CONTOURS  t1=%.0f t2=%.0f  |  B=blend:%s  C=color  Space=%s",
             threshold1, threshold2,
             blendMode ? "ON" : "OFF",
             paused ? "PAUSED" : "PLAYING");
    canvas.fillText(label, pad, textY);
}

VIVID_CHAIN(setup, update)
