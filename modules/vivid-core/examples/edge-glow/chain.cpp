// Edge Glow - Vivid Example
// Demonstrates: Edge, SolidColor, Composite, Bloom
//
// Creates neon outline effects using edge detection

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Image source
    auto& source = chain.add<Image>("source");
    source.file = "assets/photo.jpg";

    // ----- EDGE DETECTION -----
    auto& edges = chain.add<Edge>("edges");
    edges.input("source");
    edges.strength = 3.0f;
    edges.threshold = 0.08f;
    edges.invert = false;  // White edges on black

    // ----- COLORIZE EDGES -----
    // Edge outputs grayscale - use SolidColor + Multiply to colorize
    auto& neonColor = chain.add<SolidColor>("neonColor");
    neonColor.color.set(0.0f, 1.0f, 1.0f, 1.0f);  // Cyan

    auto& coloredEdges = chain.add<Composite>("coloredEdges");
    coloredEdges.inputA("edges");
    coloredEdges.inputB("neonColor");
    coloredEdges.mode = BlendMode::Multiply;

    // ----- BLOOM FOR GLOW -----
    auto& glow = chain.add<Bloom>("glow");
    glow.input("coloredEdges");
    glow.threshold = 0.1f;
    glow.intensity = 3.0f;
    glow.radius = 25.0f;
    glow.passes = 4;

    // ----- FINAL COMPOSITE -----
    // Add glowing edges over darkened original
    auto& darkened = chain.add<Brightness>("darkened");
    darkened.input("source");
    darkened.brightness = -0.3f;
    darkened.contrast = 0.8f;

    auto& final_comp = chain.add<Composite>("final");
    final_comp.inputA("darkened");
    final_comp.inputB("glow");
    final_comp.mode = BlendMode::Add;

    // Canvas for comparison
    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(ctx.width(), ctx.height());
    canvas.input(0, "source");
    canvas.input(1, "coloredEdges");
    canvas.input(2, "glow");
    canvas.input(3, "final");

    chain.output("canvas");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    // Animate neon color - cycle through hues
    float hue = std::fmod(t * 0.15f, 1.0f);
    // HSV to RGB (simplified - full saturation, full value)
    float r, g, b;
    int i = static_cast<int>(hue * 6.0f);
    float f = hue * 6.0f - i;
    switch (i % 6) {
        case 0: r = 1.0f; g = f;      b = 0.0f; break;
        case 1: r = 1.0f - f; g = 1.0f; b = 0.0f; break;
        case 2: r = 0.0f; g = 1.0f; b = f;      break;
        case 3: r = 0.0f; g = 1.0f - f; b = 1.0f; break;
        case 4: r = f;      g = 0.0f; b = 1.0f; break;
        default: r = 1.0f; g = 0.0f; b = 1.0f - f; break;
    }

    auto& neonColor = chain.get<SolidColor>("neonColor");
    neonColor.color.set(r, g, b, 1.0f);

    // Pulse glow intensity
    auto& glow = chain.get<Bloom>("glow");
    glow.intensity = 2.5f + std::sin(t * 1.5f) * 1.0f;
    glow.radius = 20.0f + std::sin(t * 0.8f) * 10.0f;

    // Draw 2x2 comparison grid
    auto& canvas = chain.get<Canvas>("canvas");
    canvas.clear(0.02f, 0.02f, 0.04f, 1.0f);

    int w = ctx.width();
    int h = ctx.height();
    int halfW = w / 2;
    int halfH = h / 2;
    int pad = 8;

    auto& source = chain.get<Image>("source");
    auto& coloredEdges = chain.get<Composite>("coloredEdges");
    auto& final_comp = chain.get<Composite>("final");

    // Top-left: Original
    canvas.drawImage(source, pad, pad, halfW - pad * 2, halfH - pad * 2);

    // Top-right: Colored edges
    canvas.drawImage(coloredEdges, halfW + pad, pad, halfW - pad * 2, halfH - pad * 2);

    // Bottom-left: Edges with bloom
    canvas.drawImage(glow, pad, halfH + pad, halfW - pad * 2, halfH - pad * 2);

    // Bottom-right: Final composite
    canvas.drawImage(final_comp, halfW + pad, halfH + pad, halfW - pad * 2, halfH - pad * 2);

    // Labels
    canvas.fillStyle(0.0f, 0.0f, 0.0f, 0.7f);
    canvas.fillRect(pad, pad, 70, 22);
    canvas.fillRect(halfW + pad, pad, 110, 22);
    canvas.fillRect(pad, halfH + pad, 130, 22);
    canvas.fillRect(halfW + pad, halfH + pad, 140, 22);

    canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);
    canvas.fillText("Original", pad + 5, pad + 16);
    canvas.fillText("Colored Edges", halfW + pad + 5, pad + 16);
    canvas.fillText("Edges + Bloom", pad + 5, halfH + pad + 16);
    canvas.fillText("Neon Glow Final", halfW + pad + 5, halfH + pad + 16);
}

VIVID_CHAIN(setup, update)
