// Threshold Example
// Demonstrates: Threshold operator for binary thresholding
//
// Shows how to convert video/images to black and white with adjustable threshold

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/video/video.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::video;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Video source - replace with your video file
    auto& video = chain.add<VideoPlayer>("video");
    video.setFile("assets/video.mp4");
    video.setLoop(true);
    video.play();

    // Threshold effects with different settings
    auto& thresh1 = chain.add<Threshold>("thresh1");
    thresh1.input("video");
    thresh1.threshold = 0.5f;   // Mid-gray cutoff
    thresh1.softness = 0.0f;    // Hard edge
    thresh1.invert = 0.0f;

    auto& thresh2 = chain.add<Threshold>("thresh2");
    thresh2.input("video");
    thresh2.threshold = 0.3f;   // Lower threshold (more white)
    thresh2.softness = 0.1f;    // Soft edge
    thresh2.invert = 0.0f;

    auto& thresh3 = chain.add<Threshold>("thresh3");
    thresh3.input("video");
    thresh3.threshold = 0.5f;
    thresh3.softness = 0.0f;
    thresh3.invert = 1.0f;      // Inverted

    auto& thresh4 = chain.add<Threshold>("thresh4");
    thresh4.input("video");
    thresh4.threshold = 0.5f;
    thresh4.softness = 0.3f;    // Very soft edge (posterization effect)
    thresh4.invert = 0.0f;

    auto& thresh5 = chain.add<Threshold>("thresh5");
    thresh5.input("video");
    thresh5.threshold = 0.7f;   // High threshold (more black)
    thresh5.softness = 0.0f;
    thresh5.invert = 0.0f;

    // Canvas for grid layout
    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(ctx.width(), ctx.height());
    canvas.input(0, "video");
    canvas.input(1, "thresh1");
    canvas.input(2, "thresh2");
    canvas.input(3, "thresh3");
    canvas.input(4, "thresh4");
    canvas.input(5, "thresh5");

    chain.output("canvas");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    // Animate threshold on first example
    auto& thresh1 = chain.get<Threshold>("thresh1");
    thresh1.threshold = 0.5f + std::sin(t * 0.5f) * 0.3f;  // 0.2 to 0.8

    // Draw 2x3 grid (original + 4 threshold variations)
    auto& canvas = chain.get<Canvas>("canvas");
    canvas.clear(0.1f, 0.1f, 0.1f, 1.0f);

    int w = ctx.width();
    int h = ctx.height();
    int cellW = w / 3;
    int cellH = h / 2;
    int pad = 6;

    auto& video = chain.get<VideoPlayer>("video");
    auto& thresh2 = chain.get<Threshold>("thresh2");
    auto& thresh3 = chain.get<Threshold>("thresh3");
    auto& thresh4 = chain.get<Threshold>("thresh4");
    auto& thresh5 = chain.get<Threshold>("thresh5");

    // Top row: Original, Animated Threshold, Low Threshold
    canvas.drawImage(video, pad, pad, cellW - pad * 2, cellH - pad * 2);
    canvas.drawImage(thresh1, cellW + pad, pad, cellW - pad * 2, cellH - pad * 2);
    canvas.drawImage(thresh2, cellW * 2 + pad, pad, cellW - pad * 2, cellH - pad * 2);

    // Bottom row: Inverted, Soft Edge, High Threshold
    canvas.drawImage(thresh3, pad, cellH + pad, cellW - pad * 2, cellH - pad * 2);
    canvas.drawImage(thresh4, cellW + pad, cellH + pad, cellW - pad * 2, cellH - pad * 2);
    canvas.drawImage(thresh5, cellW * 2 + pad, cellH + pad, cellW - pad * 2, cellH - pad * 2);

    // Labels - use font metrics for proper positioning
    canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);
    auto fm = canvas.fontMetrics();
    float textY = pad + fm.ascent;
    float textY2 = cellH + pad + fm.ascent;

    canvas.fillText("Original", pad + 5, textY);

    char label1[64];
    snprintf(label1, sizeof(label1), "thresh=%.2f (animated)", static_cast<float>(thresh1.threshold));
    canvas.fillText(label1, cellW + pad + 5, textY);

    canvas.fillText("thresh=0.3 soft=0.1", cellW * 2 + pad + 5, textY);
    canvas.fillText("thresh=0.5 INVERTED", pad + 5, textY2);
    canvas.fillText("thresh=0.5 soft=0.3", cellW + pad + 5, textY2);
    canvas.fillText("thresh=0.7 (high)", cellW * 2 + pad + 5, textY2);
}

VIVID_CHAIN(setup, update)
