// Generators Example
// Demonstrates all core generator operators: SolidColor, Gradient, Ramp, Shape, LFO
//
// This example shows a 2x3 grid of generators with LFO modulation

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // LFO for modulation (outputs -1 to 1)
    // This creates visible dashed connections in the chain visualizer
    auto& lfo = chain.add<LFO>("lfo");
    lfo.frequency = 0.5f;
    lfo.waveform(LFOWaveform::Sine);

    // 1. SolidColor - Simple flat color (animated hue)
    auto& solid = chain.add<SolidColor>("solid");
    solid.color.set(1.0f, 0.5f, 0.2f, 1.0f);

    // 2. Gradient - Linear gradient between two colors
    // Angle modulated by LFO (TRACKABLE - shows as dashed line in visualizer!)
    auto& gradient = chain.add<Gradient>("gradient");
    gradient.colorA.set(1.0f, 0.2f, 0.5f, 1.0f);
    gradient.colorB.set(0.2f, 0.5f, 1.0f, 1.0f);
    // Bind angle: LFO -1..1 maps to 0..π radians
    gradient.angle.bind(lfo, -1.0f, 1.0f, 0.0f, 3.14159f);

    // 3. Ramp - Animated HSV gradient
    auto& ramp = chain.add<Ramp>("ramp");
    ramp.hueSpeed = 0.2f;
    ramp.saturation = 0.8f;
    ramp.brightness = 0.9f;

    // 4. Shape - SDF circle
    // Position modulated by LFO (TRACKABLE - shows as dashed line in visualizer!)
    auto& circle = chain.add<Shape>("circle");
    circle.type(ShapeType::Circle);
    circle.size.set(0.4f, 0.4f);  // Square size - will render as circle
    circle.color.set(1.0f, 0.8f, 0.2f, 1.0f);
    circle.softness = 0.02f;
    // Bind X position: LFO -1..1 maps to 0.4..0.6
    circle.position.bindX(lfo, -1.0f, 1.0f, 0.4f, 0.6f);

    // 5. Shape - SDF rectangle (wider than tall)
    auto& rect = chain.add<Shape>("rect");
    rect.type(ShapeType::Rectangle);
    rect.size.set(0.5f, 0.35f);  // Wider rectangle
    rect.color.set(0.2f, 1.0f, 0.6f, 1.0f);
    rect.cornerRadius = 0.05f;

    // 6. Shape - SDF polygon (5-sided star)
    auto& star = chain.add<Shape>("star");
    star.type(ShapeType::Polygon);
    star.sides = 5;
    star.size.set(0.45f, 0.45f);
    star.color.set(1.0f, 0.4f, 0.8f, 1.0f);

    // Create 2x3 grid using Canvas
    // NOTE: Canvas.drawImage() requires operators to be processed first.
    // Use input() to establish dependencies for correct processing order.
    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(ctx.width(), ctx.height());
    canvas.input(0, "lfo");      // LFO must be processed before bound params evaluate
    canvas.input(1, "solid");
    canvas.input(2, "gradient");
    canvas.input(3, "ramp");
    canvas.input(4, "circle");
    canvas.input(5, "rect");
    canvas.input(6, "star");

    chain.output("canvas");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    // LFO-bound params (gradient.angle, circle.position.x) update automatically!
    // Get LFO value for debug display
    auto& lfo = chain.get<LFO>("lfo");
    float lfoVal = lfo.value();

    // Animate solid color hue (time-based, not LFO)
    auto& solid = chain.get<SolidColor>("solid");
    float hue = std::fmod(ctx.time() * 0.1f, 1.0f);
    solid.color.set(
        0.5f + 0.5f * std::sin(hue * 6.28f),
        0.5f + 0.5f * std::sin(hue * 6.28f + 2.09f),
        0.5f + 0.5f * std::sin(hue * 6.28f + 4.18f),
        1.0f
    );

    // gradient.angle is auto-bound to LFO (no manual update needed!)

    // circle.position.x is auto-bound to LFO (no manual update needed!)

    auto& star = chain.get<Shape>("star");
    star.rotation = ctx.time() * 0.5f;  // radians

    // Draw 2x3 grid
    auto& canvas = chain.get<Canvas>("canvas");
    canvas.clear(0.1f, 0.1f, 0.12f, 1.0f);

    int w = ctx.width();
    int h = ctx.height();
    float cellW = w / 3.0f;
    float cellH = h / 2.0f;

    auto& gradient = chain.get<Gradient>("gradient");
    auto& ramp = chain.get<Ramp>("ramp");
    auto& circle = chain.get<Shape>("circle");
    auto& rect = chain.get<Shape>("rect");

    // Row 1: SolidColor, Gradient, Ramp
    // Use Stretch mode for gradients since they look fine stretched
    canvas.setFitMode(FitMode::Stretch);
    canvas.drawImage(solid, 0, 0, cellW, cellH);
    canvas.drawImage(gradient, cellW, 0, cellW, cellH);
    canvas.drawImage(ramp, cellW * 2, 0, cellW, cellH);

    // Row 2: Shapes - use Fit mode to preserve aspect ratio (circles stay circular!)
    canvas.setFitMode(FitMode::Fit);
    canvas.drawImage(circle, 0, cellH, cellW, cellH);
    canvas.drawImage(rect, cellW, cellH, cellW, cellH);
    canvas.drawImage(star, cellW * 2, cellH, cellW, cellH);

    // Helper to draw label with background box (x, y is top-left of box)
    auto drawLabel = [&](const char* text, float x, float y) {
        glm::vec2 size = canvas.measureText(text);
        float pad = 4.0f;
        canvas.fillStyle(0.0f, 0.0f, 0.0f, 0.6f);
        canvas.fillRect(x, y, size.x + pad * 2, size.y + pad * 2);
        canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);
        canvas.fillText(text, x + pad, y + size.y + pad);
    };

    // Row 1 labels (top-left of each cell)
    drawLabel("SolidColor", 4, 4);
    drawLabel("Gradient", cellW + 4, 4);
    drawLabel("Ramp", cellW * 2 + 4, 4);

    // Row 2 labels (top-left of each cell)
    drawLabel("Shape: Circle", 4, cellH + 4);
    drawLabel("Shape: Rectangle", cellW + 4, cellH + 4);
    drawLabel("Shape: Polygon", cellW * 2 + 4, cellH + 4);

    ctx.debug("time", t);
    ctx.debug("lfo", lfoVal);
}

VIVID_CHAIN(setup, update)
