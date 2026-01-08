// Edge Glow - Vivid Example
// Demonstrates: Edge, Brightness, Math, Bloom
//
// Creates neon outline effects using edge detection

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Create interesting geometry for edge detection
    auto& bg = chain.add<SolidColor>("bg");
    bg.color.set(0.02f, 0.02f, 0.05f, 1.0f);  // Near black

    // Multiple shapes for complex edges
    auto& shape1 = chain.add<Shape>("shape1");
    shape1.type(ShapeType::Star);
    shape1.sides = 5;
    shape1.size.set(0.3f, 0.3f);
    shape1.softness = 0.02f;
    shape1.color.set(0.4f, 0.4f, 0.4f, 1.0f);

    auto& shape2 = chain.add<Shape>("shape2");
    shape2.type(ShapeType::Polygon);
    shape2.sides = 6;
    shape2.size.set(0.2f, 0.2f);
    shape2.position.set(-0.25f, 0.15f);
    shape2.softness = 0.02f;
    shape2.color.set(0.5f, 0.5f, 0.5f, 1.0f);

    auto& shape3 = chain.add<Shape>("shape3");
    shape3.type(ShapeType::Circle);
    shape3.size.set(0.15f, 0.15f);
    shape3.position.set(0.25f, -0.15f);
    shape3.softness = 0.05f;  // Softer for gradient edges
    shape3.color.set(0.6f, 0.6f, 0.6f, 1.0f);

    // Composite shapes onto background
    auto& comp1 = chain.add<Composite>("comp1");
    comp1.inputA("bg");
    comp1.inputB("shape1");
    comp1.mode(BlendMode::Add);

    auto& comp2 = chain.add<Composite>("comp2");
    comp2.inputA("comp1");
    comp2.inputB("shape2");
    comp2.mode(BlendMode::Add);

    auto& source = chain.add<Composite>("source");
    source.inputA("comp2");
    source.inputB("shape3");
    source.mode(BlendMode::Add);

    // ----- EDGE DETECTION -----
    // Sobel operator detects edges (gradients) in the image
    auto& edges = chain.add<Edge>("edges");
    edges.input("source");
    edges.strength = 2.0f;     // Edge intensity multiplier
    edges.threshold = 0.05f;   // Minimum edge value to show
    edges.invert = false;      // White edges on black

    // ----- BRIGHTNESS ADJUSTMENT -----
    // Boost edge contrast for glow effect
    auto& bright = chain.add<Brightness>("bright");
    bright.input("edges");
    bright.brightness = 0.1f;
    bright.contrast = 1.5f;

    // ----- HSV COLORING -----
    // Tint edges with neon color
    auto& hsv = chain.add<HSV>("hsv");
    hsv.input("bright");
    hsv.saturation = 2.0f;  // Boost saturation for color

    // ----- BLOOM FOR GLOW -----
    // Add glow to the edges
    auto& glow = chain.add<Bloom>("glow");
    glow.input("hsv");
    glow.threshold = 0.2f;
    glow.intensity = 2.5f;
    glow.radius = 20.0f;
    glow.passes = 3;

    // ----- INVERTED EDGES (white background) -----
    auto& edges_inv = chain.add<Edge>("edges_inv");
    edges_inv.input("source");
    edges_inv.strength = 1.5f;
    edges_inv.threshold = 0.1f;
    edges_inv.invert = true;  // Black edges on white

    // ----- FINAL COMPOSITE -----
    // Combine original with glowing edges
    auto& final_comp = chain.add<Composite>("final");
    final_comp.inputA("source");
    final_comp.inputB("glow");
    final_comp.mode(BlendMode::Add);

    // Canvas for comparison
    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(ctx.width(), ctx.height());
    canvas.input(0, "source");
    canvas.input(1, "edges");
    canvas.input(2, "edges_inv");
    canvas.input(3, "final");

    chain.output("canvas");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    // Animate shapes
    auto& shape1 = chain.get<Shape>("shape1");
    shape1.rotation = t * 0.2f;
    float pulse1 = 0.25f + 0.08f * std::sin(t * 1.2f);
    shape1.size.set(pulse1, pulse1);

    auto& shape2 = chain.get<Shape>("shape2");
    shape2.rotation = -t * 0.3f;
    shape2.position.set(-0.25f + std::sin(t * 0.5f) * 0.1f,
                        0.15f + std::cos(t * 0.7f) * 0.1f);

    auto& shape3 = chain.get<Shape>("shape3");
    shape3.position.set(0.25f + std::cos(t * 0.6f) * 0.12f,
                        -0.15f + std::sin(t * 0.8f) * 0.12f);

    // Mouse controls edge parameters
    float mouseX = ctx.mouseNorm().x * 0.5f + 0.5f;  // 0-1
    float mouseY = ctx.mouseNorm().y * 0.5f + 0.5f;  // 0-1

    // X: edge strength (0.5-4)
    float strength = 0.5f + mouseX * 3.5f;

    // Y: edge threshold (0-0.3)
    float threshold = mouseY * 0.3f;

    auto& edges = chain.get<Edge>("edges");
    edges.strength = strength;
    edges.threshold = threshold;

    auto& edges_inv = chain.get<Edge>("edges_inv");
    edges_inv.strength = strength;
    edges_inv.threshold = threshold;

    // Animate glow color via HSV hue shift
    auto& hsv = chain.get<HSV>("hsv");
    hsv.hueShift = std::fmod(t * 0.1f, 1.0f);  // Cycle through colors

    // Animate glow intensity
    auto& glow = chain.get<Bloom>("glow");
    glow.intensity = 2.0f + std::sin(t * 0.8f) * 1.0f;
    glow.radius = 15.0f + std::sin(t * 0.5f) * 10.0f;

    // Draw 2x2 comparison grid
    auto& canvas = chain.get<Canvas>("canvas");
    canvas.clear(0.03f, 0.03f, 0.05f, 1.0f);

    int w = ctx.width();
    int h = ctx.height();
    int halfW = w / 2;
    int halfH = h / 2;
    int pad = 8;

    auto& source = chain.get<Composite>("source");
    auto& final_comp = chain.get<Composite>("final");

    // Top-left: Original
    canvas.drawImage(source, pad, pad, halfW - pad * 2, halfH - pad * 2);

    // Top-right: Edge detection (normal)
    canvas.drawImage(edges, halfW + pad, pad, halfW - pad * 2, halfH - pad * 2);

    // Bottom-left: Edge detection (inverted)
    canvas.drawImage(edges_inv, pad, halfH + pad, halfW - pad * 2, halfH - pad * 2);

    // Bottom-right: Final (original + neon glow)
    canvas.drawImage(final_comp, halfW + pad, halfH + pad, halfW - pad * 2, halfH - pad * 2);

    // Labels
    canvas.fillStyle(0.0f, 0.0f, 0.0f, 0.7f);
    canvas.fillRect(pad, pad, 70, 22);
    canvas.fillRect(halfW + pad, pad, 220, 22);
    canvas.fillRect(pad, halfH + pad, 130, 22);
    canvas.fillRect(halfW + pad, halfH + pad, 150, 22);

    canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);
    canvas.fillText("Original", pad + 5, pad + 16);

    char edgeLabel[64];
    snprintf(edgeLabel, sizeof(edgeLabel), "Edge: strength=%.1f thresh=%.2f", strength, threshold);
    canvas.fillText(edgeLabel, halfW + pad + 5, pad + 16);

    canvas.fillText("Edge (inverted)", pad + 5, halfH + pad + 16);
    canvas.fillText("Neon Glow Effect", halfW + pad + 5, halfH + pad + 16);
}

VIVID_CHAIN(setup, update)
