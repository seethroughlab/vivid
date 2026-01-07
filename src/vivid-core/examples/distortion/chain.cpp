// Distortion Effects Example
// Demonstrates: ChromaticAberration, BarrelDistortion, Displace, Edge
//
// Shows various spatial distortion and edge detection effects

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Source: colorful gradient (good for showing distortion)
    auto& source = chain.add<Ramp>("source");
    source.hueSpeed = 0.1f;

    // Overlay a grid pattern for distortion visibility
    auto& grid = chain.add<Shape>("grid");
    grid.type(ShapeType::Rectangle);
    grid.position.set(0.0f, 0.0f);
    grid.size.set(0.4f, 0.4f);
    grid.color.set(1.0f, 1.0f, 1.0f, 0.3f);

    // Composite grid onto gradient
    auto& base = chain.add<Composite>("base");
    base.inputA("source");
    base.inputB("grid");
    base.mode(BlendMode::Add);

    // Chromatic Aberration - RGB channel separation
    auto& chroma = chain.add<ChromaticAberration>("chroma");
    chroma.input("base");
    chroma.amount = 0.02f;
    chroma.radial = true;

    // Barrel Distortion - CRT-style curvature
    auto& barrel = chain.add<BarrelDistortion>("barrel");
    barrel.input("base");
    barrel.curvature = 0.15f;

    // Edge Detection - Sobel operator
    auto& edge = chain.add<Edge>("edge");
    edge.input("base");
    edge.strength = 2.0f;
    edge.threshold = 0.05f;
    edge.invert = false;

    // Displacement Map - use animated noise
    auto& dispMap = chain.add<Noise>("dispMap");
    dispMap.scale = 3.0f;
    dispMap.speed = 0.3f;
    dispMap.octaves = 2;

    // Displace - distort using displacement map
    auto& displace = chain.add<Displace>("displace");
    displace.source("base");      // Source to distort
    displace.map("dispMap");      // Displacement map (R=X, G=Y)
    displace.strength = 0.08f;
    displace.strengthX = 1.0f;
    displace.strengthY = 1.0f;

    // Canvas for grid layout
    // NOTE: Canvas.drawImage() requires operators to be processed first.
    // Use input() to establish dependencies for correct processing order.
    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(ctx.width(), ctx.height());
    canvas.input(0, "chroma");
    canvas.input(1, "barrel");
    canvas.input(2, "edge");
    canvas.input(3, "displace");

    chain.output("canvas");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    // Animate chromatic aberration
    auto& chroma = chain.get<ChromaticAberration>("chroma");
    chroma.amount = 0.01f + std::sin(t * 0.4f) * 0.015f;  // 0.0 to 0.025
    chroma.angle = t * 0.5f;  // Rotating direction (for linear mode)

    // Animate barrel distortion
    auto& barrel = chain.get<BarrelDistortion>("barrel");
    barrel.curvature = 0.1f + std::sin(t * 0.3f) * 0.08f;  // 0.02 to 0.18

    // Animate edge detection
    auto& edge = chain.get<Edge>("edge");
    edge.strength = 1.5f + std::sin(t * 0.5f) * 1.0f;  // 0.5 to 2.5
    edge.threshold = 0.1f + std::sin(t * 0.2f) * 0.08f;  // 0.02 to 0.18

    // Animate displacement
    auto& displace = chain.get<Displace>("displace");
    displace.strength = 0.05f + std::sin(t * 0.35f) * 0.04f;  // 0.01 to 0.09
    displace.strengthX = 1.0f + std::sin(t * 0.6f) * 0.5f;   // 0.5 to 1.5
    displace.strengthY = 1.0f + std::cos(t * 0.6f) * 0.5f;   // 0.5 to 1.5

    // Draw 2x2 grid
    auto& canvas = chain.get<Canvas>("canvas");
    canvas.clear(0.05f, 0.05f, 0.07f, 1.0f);

    int w = ctx.width();
    int h = ctx.height();
    int halfW = w / 2;
    int halfH = h / 2;
    int pad = 8;

    // Top-left: Chromatic Aberration
    canvas.drawImage(chroma, pad, pad, halfW - pad * 2, halfH - pad * 2);

    // Top-right: Barrel Distortion
    canvas.drawImage(barrel, halfW + pad, pad, halfW - pad * 2, halfH - pad * 2);

    // Bottom-left: Edge Detection
    canvas.drawImage(edge, pad, halfH + pad, halfW - pad * 2, halfH - pad * 2);

    // Bottom-right: Displacement
    canvas.drawImage(displace, halfW + pad, halfH + pad, halfW - pad * 2, halfH - pad * 2);

    // Labels
    canvas.fillStyle(0.0f, 0.0f, 0.0f, 0.7f);
    canvas.fillRect(pad, pad, 200, 22);
    canvas.fillRect(halfW + pad, pad, 170, 22);
    canvas.fillRect(pad, halfH + pad, 180, 22);
    canvas.fillRect(halfW + pad, halfH + pad, 180, 22);

    canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);

    char chromaLabel[64];
    snprintf(chromaLabel, sizeof(chromaLabel), "ChromaticAberration: %.3f",
        static_cast<float>(chroma.amount));
    canvas.fillText(chromaLabel, pad + 5, pad + 16);

    char barrelLabel[64];
    snprintf(barrelLabel, sizeof(barrelLabel), "BarrelDistortion: %.2f",
        static_cast<float>(barrel.curvature));
    canvas.fillText(barrelLabel, halfW + pad + 5, pad + 16);

    char edgeLabel[64];
    snprintf(edgeLabel, sizeof(edgeLabel), "Edge: str=%.1f thr=%.2f",
        static_cast<float>(edge.strength), static_cast<float>(edge.threshold));
    canvas.fillText(edgeLabel, pad + 5, halfH + pad + 16);

    char displaceLabel[64];
    snprintf(displaceLabel, sizeof(displaceLabel), "Displace: str=%.2f",
        static_cast<float>(displace.strength));
    canvas.fillText(displaceLabel, halfW + pad + 5, halfH + pad + 16);
}

VIVID_CHAIN(setup, update)
