// Time Effects - Vivid Example
// Demonstrates: TimeMachine, FrameCache, VideoPlayer
//
// Creates slit-scan and temporal displacement effects

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/video/video.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::video;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Video source - movement is essential for time effects
    auto& source = chain.add<VideoPlayer>("source");
    source.setFile("assets/movie.mp4");
    source.setLoop(true);
    source.play();

    // ----- FRAME CACHE -----
    // Stores N frames of history for temporal effects
    // More frames = more dramatic time displacement visible
    auto& cache = chain.add<FrameCache>("cache");
    cache.input("source");
    cache.frameCount = 90;  // 3 seconds at 30fps - very visible effect

    // ----- DISPLACEMENT MAPS -----
    // Different displacement patterns create different effects

    // Vertical gradient: classic slit-scan (horizontal slices from different times)
    auto& gradient_v = chain.add<Gradient>("gradient_v");
    gradient_v.mode = GradientMode::Linear;
    gradient_v.angle = 1.5708f;  // π/2 = bottom to top
    gradient_v.colorA.set(0.0f, 0.0f, 0.0f, 1.0f);  // Black (old frames)
    gradient_v.colorB.set(1.0f, 1.0f, 1.0f, 1.0f);  // White (new frames)

    // Horizontal gradient: vertical slit-scan
    auto& gradient_h = chain.add<Gradient>("gradient_h");
    gradient_h.mode = GradientMode::Linear;
    gradient_h.angle = 0.0f;  // 0 = left to right
    gradient_h.colorA.set(0.0f, 0.0f, 0.0f, 1.0f);
    gradient_h.colorB.set(1.0f, 1.0f, 1.0f, 1.0f);

    // Radial gradient: center shows current, edges show past
    auto& gradient_r = chain.add<Gradient>("gradient_r");
    gradient_r.mode = GradientMode::Radial;
    gradient_r.colorA.set(1.0f, 1.0f, 1.0f, 1.0f);  // Center = current
    gradient_r.colorB.set(0.0f, 0.0f, 0.0f, 1.0f);  // Edge = past

    // Noise-based displacement: organic time distortion
    auto& disp_noise = chain.add<Noise>("disp_noise");
    disp_noise.scale = 2.0f;
    disp_noise.speed = 0.3f;
    disp_noise.type = NoiseType::Perlin;

    // ----- TIME MACHINE EFFECTS -----
    // Each uses the same cache but different displacement maps

    // Horizontal slit-scan (most classic)
    auto& slit_h = chain.add<TimeMachine>("slit_h");
    slit_h.cache(&cache);
    slit_h.displacementMap(&gradient_v);
    slit_h.depth = 1.0f;

    // Vertical slit-scan
    auto& slit_v = chain.add<TimeMachine>("slit_v");
    slit_v.cache(&cache);
    slit_v.displacementMap(&gradient_h);
    slit_v.depth = 1.0f;

    // Radial time warp
    auto& radial = chain.add<TimeMachine>("radial");
    radial.cache(&cache);
    radial.displacementMap(&gradient_r);
    radial.depth = 1.0f;

    // Organic time distortion
    auto& organic = chain.add<TimeMachine>("organic");
    organic.cache(&cache);
    organic.displacementMap(&disp_noise);
    organic.depth = 0.8f;

    // Canvas for 2x2 comparison
    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(ctx.width(), ctx.height());
    canvas.input(0, "slit_h");
    canvas.input(1, "slit_v");
    canvas.input(2, "radial");
    canvas.input(3, "organic");

    chain.output("canvas");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    // Animate depth automatically to show effect clearly
    // Oscillates between 0.3 and 1.0 for visible time displacement
    float depth = 0.65f + 0.35f * std::sin(t * 0.5f);

    // Mouse Y can still adjust offset
    float mouseY = ctx.mouseNorm().y * 0.5f + 0.5f;
    float offset = mouseY * 0.3f;

    auto& slit_h = chain.get<TimeMachine>("slit_h");
    slit_h.depth = depth;
    slit_h.offset = 0.0f;  // No offset for cleaner slit-scan

    auto& slit_v = chain.get<TimeMachine>("slit_v");
    slit_v.depth = depth;
    slit_v.offset = 0.0f;

    auto& radial = chain.get<TimeMachine>("radial");
    radial.depth = depth;
    radial.offset = 0.0f;

    auto& organic = chain.get<TimeMachine>("organic");
    organic.depth = depth;
    organic.offset = offset;

    // Animate displacement noise for organic effect
    auto& disp_noise = chain.get<Noise>("disp_noise");
    disp_noise.scale = 3.0f + std::sin(t * 0.3f);
    disp_noise.speed = 0.5f;

    // Draw 2x2 grid
    auto& canvas = chain.get<Canvas>("canvas");
    canvas.clear(0.1f, 0.1f, 0.12f, 1.0f);

    int w = ctx.width();
    int h = ctx.height();
    int halfW = w / 2;
    int halfH = h / 2;
    int pad = 10;
    int labelH = 32;

    // Draw images FIRST
    canvas.drawImage(slit_h, pad, pad + labelH, halfW - pad * 2, halfH - pad * 2 - labelH);
    canvas.drawImage(slit_v, halfW + pad, pad + labelH, halfW - pad * 2, halfH - pad * 2 - labelH);
    canvas.drawImage(radial, pad, halfH + pad + labelH, halfW - pad * 2, halfH - pad * 2 - labelH);
    canvas.drawImage(organic, halfW + pad, halfH + pad + labelH, halfW - pad * 2, halfH - pad * 2 - labelH);

    // Draw labels ON TOP of images
    // Label backgrounds
    canvas.fillStyle(0.0f, 0.0f, 0.0f, 0.85f);
    canvas.fillRect(pad, pad, halfW - pad * 2, labelH);
    canvas.fillRect(halfW + pad, pad, halfW - pad * 2, labelH);
    canvas.fillRect(pad, halfH + pad, halfW - pad * 2, labelH);
    canvas.fillRect(halfW + pad, halfH + pad, halfW - pad * 2, labelH);

    // Label text
    canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);

    char label1[64];
    snprintf(label1, sizeof(label1), "HORIZONTAL SLIT-SCAN  depth=%.0f%%", depth * 100);
    canvas.fillText(label1, pad + 8, pad + 22);

    canvas.fillText("VERTICAL SLIT-SCAN", halfW + pad + 8, pad + 22);
    canvas.fillText("RADIAL TIME WARP", pad + 8, halfH + pad + 22);
    canvas.fillText("ORGANIC DISTORTION", halfW + pad + 8, halfH + pad + 22);

    // Add hint at bottom
    canvas.fillStyle(0.5f, 0.5f, 0.5f, 1.0f);
    canvas.fillText("Move mouse up/down to adjust organic offset", w/2 - 180, h - 20);
}

VIVID_CHAIN(setup, update)
