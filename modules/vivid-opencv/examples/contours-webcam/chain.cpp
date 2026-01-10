// Contours - Webcam
// Real-time contour detection from webcam feed using OpenCV
//
// Uses Canny edge detection followed by contour extraction.
// Shows both the original webcam feed and detected contours side by side.
//
// Controls:
//   Mouse X: Canny threshold 1
//   Mouse Y: Canny threshold 2
//   1-4: Contour mode (External, List, CComp, Tree)
//   +/-: Increase/decrease line width

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/video/video.h>
#include <vivid/opencv/opencv.h>
#include <cmath>
#include <iostream>

using namespace vivid;
using namespace vivid::effects;

static int contourMode = 0;
static float lineWidth = 2.0f;

void printControls() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Contours - Webcam" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  Mouse X: Canny threshold 1 (0-255)" << std::endl;
    std::cout << "  Mouse Y: Canny threshold 2 (0-255)" << std::endl;
    std::cout << "  1: External contours only" << std::endl;
    std::cout << "  2: List all contours" << std::endl;
    std::cout << "  3: Two-level hierarchy" << std::endl;
    std::cout << "  4: Full tree hierarchy" << std::endl;
    std::cout << "  +/-: Line width" << std::endl;
    std::cout << "========================================\n" << std::endl;
}

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    printControls();

    // =========================================================================
    // Video Source - Webcam
    // =========================================================================

    auto& cam = chain.add<vivid::video::Webcam>("cam");
    cam.setResolution(1280, 720);
    cam.setFrameRate(30.0f);

    // =========================================================================
    // OpenCV Contour Detection
    // =========================================================================

    auto& contours = chain.add<vivid::opencv::Contours>("contours");
    contours.input("cam");
    contours.threshold1 = 50.0f;
    contours.threshold2 = 150.0f;
    contours.mode = contourMode;
    contours.lineWidth = lineWidth;
    // Green contours on transparent background
    contours.colorR = 0.0f;
    contours.colorG = 1.0f;
    contours.colorB = 0.0f;
    contours.colorA = 1.0f;

    // =========================================================================
    // Compositing - Side by side view
    // =========================================================================

    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(ctx.width(), ctx.height());
    canvas.input(0, "cam");
    canvas.input(1, "contours");

    chain.output("canvas");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // Input Handling
    // =========================================================================

    // Mode selection (1-4 keys)
    if (ctx.key(GLFW_KEY_1).pressed) contourMode = 0;
    if (ctx.key(GLFW_KEY_2).pressed) contourMode = 1;
    if (ctx.key(GLFW_KEY_3).pressed) contourMode = 2;
    if (ctx.key(GLFW_KEY_4).pressed) contourMode = 3;

    // Line width adjustment
    if (ctx.key(GLFW_KEY_EQUAL).pressed || ctx.key(GLFW_KEY_KP_ADD).pressed) {
        lineWidth = std::min(20.0f, lineWidth + 1.0f);
    }
    if (ctx.key(GLFW_KEY_MINUS).pressed || ctx.key(GLFW_KEY_KP_SUBTRACT).pressed) {
        lineWidth = std::max(1.0f, lineWidth - 1.0f);
    }

    // =========================================================================
    // Mouse Controls - Canny Thresholds
    // =========================================================================

    glm::vec2 mouse = ctx.mouseNorm();
    float threshold1 = mouse.x * 255.0f;
    float threshold2 = mouse.y * 255.0f;

    // =========================================================================
    // Update Contours Operator
    // =========================================================================

    auto& contours = chain.get<vivid::opencv::Contours>("contours");
    contours.threshold1 = threshold1;
    contours.threshold2 = threshold2;
    contours.mode = contourMode;
    contours.lineWidth = lineWidth;

    // =========================================================================
    // Draw Side-by-Side Comparison
    // =========================================================================

    auto& canvas = chain.get<Canvas>("canvas");
    canvas.clear(0.1f, 0.1f, 0.12f, 1.0f);

    int w = ctx.width();
    int h = ctx.height();
    int halfW = w / 2;
    int pad = 10;
    int labelH = 28;

    auto& cam = chain.get<vivid::video::Webcam>("cam");

    // Draw images
    canvas.drawImage(cam, pad, pad + labelH, halfW - pad * 2, h - pad * 2 - labelH);
    canvas.drawImage(contours, halfW + pad, pad + labelH, halfW - pad * 2, h - pad * 2 - labelH);

    // Labels
    canvas.fillStyle(0.0f, 0.0f, 0.0f, 0.85f);
    canvas.fillRect(pad, pad, halfW - pad * 2, labelH);
    canvas.fillRect(halfW + pad, pad, halfW - pad * 2, labelH);

    canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);
    auto fm = canvas.fontMetrics();
    float textY = pad + (labelH + fm.ascent - fm.descent) * 0.5f;

    canvas.fillText("WEBCAM", pad + 8, textY);

    char label[128];
    const char* modeNames[] = {"External", "List", "CComp", "Tree"};
    snprintf(label, sizeof(label), "CONTOURS  t1=%.0f t2=%.0f mode=%s width=%.0f",
             threshold1, threshold2, modeNames[contourMode], lineWidth);
    canvas.fillText(label, halfW + pad + 8, textY);
}

VIVID_CHAIN(setup, update)
