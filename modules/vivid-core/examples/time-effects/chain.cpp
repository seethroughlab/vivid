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
    auto& cache = chain.add<FrameCache>("cache");
    cache.input("source");
    cache.frameCount = 60;  // 2 seconds at 30fps

    // ----- DISPLACEMENT MAPS -----
    // Different displacement patterns create different effects

    // Vertical gradient: classic slit-scan (horizontal slices from different times)
    auto& gradient_v = chain.add<Gradient>("gradient_v");
    gradient_v.mode(GradientMode::Linear);
    gradient_v.angle = 1.5708f;  // π/2 = bottom to top
    gradient_v.colorA.set(0.0f, 0.0f, 0.0f, 1.0f);  // Black (old frames)
    gradient_v.colorB.set(1.0f, 1.0f, 1.0f, 1.0f);  // White (new frames)

    // Horizontal gradient: vertical slit-scan
    auto& gradient_h = chain.add<Gradient>("gradient_h");
    gradient_h.mode(GradientMode::Linear);
    gradient_h.angle = 0.0f;  // 0 = left to right
    gradient_h.colorA.set(0.0f, 0.0f, 0.0f, 1.0f);
    gradient_h.colorB.set(1.0f, 1.0f, 1.0f, 1.0f);

    // Radial gradient: center shows current, edges show past
    auto& gradient_r = chain.add<Gradient>("gradient_r");
    gradient_r.mode(GradientMode::Radial);
    gradient_r.colorA.set(1.0f, 1.0f, 1.0f, 1.0f);  // Center = current
    gradient_r.colorB.set(0.0f, 0.0f, 0.0f, 1.0f);  // Edge = past

    // Noise-based displacement: organic time distortion
    auto& disp_noise = chain.add<Noise>("disp_noise");
    disp_noise.scale = 2.0f;
    disp_noise.speed = 0.3f;
    disp_noise.type(NoiseType::Perlin);

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

    // Mouse controls time depth and offset
    float mouseX = ctx.mouseNorm().x * 0.5f + 0.5f;  // 0-1
    float mouseY = ctx.mouseNorm().y * 0.5f + 0.5f;  // 0-1

    // X: time depth (how far back in time to reach)
    float depth = mouseX;

    // Y: offset (bias toward newer or older frames)
    float offset = mouseY * 0.5f;

    auto& slit_h = chain.get<TimeMachine>("slit_h");
    slit_h.depth = depth;
    slit_h.offset = offset;

    auto& slit_v = chain.get<TimeMachine>("slit_v");
    slit_v.depth = depth;
    slit_v.offset = offset;

    auto& radial = chain.get<TimeMachine>("radial");
    radial.depth = depth;
    radial.offset = offset;

    auto& organic = chain.get<TimeMachine>("organic");
    organic.depth = depth * 0.8f;
    organic.offset = offset;

    // Animate displacement noise
    auto& disp_noise = chain.get<Noise>("disp_noise");
    disp_noise.scale = 1.5f + std::sin(t * 0.2f) * 0.5f;

    // Draw 2x2 grid
    auto& canvas = chain.get<Canvas>("canvas");
    canvas.clear(0.05f, 0.05f, 0.08f, 1.0f);

    int w = ctx.width();
    int h = ctx.height();
    int halfW = w / 2;
    int halfH = h / 2;
    int pad = 8;

    // Draw each effect
    canvas.drawImage(slit_h, pad, pad, halfW - pad * 2, halfH - pad * 2);
    canvas.drawImage(slit_v, halfW + pad, pad, halfW - pad * 2, halfH - pad * 2);
    canvas.drawImage(radial, pad, halfH + pad, halfW - pad * 2, halfH - pad * 2);
    canvas.drawImage(organic, halfW + pad, halfH + pad, halfW - pad * 2, halfH - pad * 2);

    // Labels
    canvas.fillStyle(0.0f, 0.0f, 0.0f, 0.7f);
    canvas.fillRect(pad, pad, 170, 22);
    canvas.fillRect(halfW + pad, pad, 160, 22);
    canvas.fillRect(pad, halfH + pad, 160, 22);
    canvas.fillRect(halfW + pad, halfH + pad, 200, 22);

    canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);

    char label1[64];
    snprintf(label1, sizeof(label1), "Horizontal Slit-Scan (depth=%.2f)", depth);
    canvas.fillText(label1, pad + 5, pad + 16);

    canvas.fillText("Vertical Slit-Scan", halfW + pad + 5, pad + 16);
    canvas.fillText("Radial Time Warp", pad + 5, halfH + pad + 16);
    canvas.fillText("Organic Time Distortion", halfW + pad + 5, halfH + pad + 16);
}

VIVID_CHAIN(setup, update)
