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

    // LFOs for animation
    auto& lfo1 = chain.add<LFO>("lfo1");
    lfo1.frequency = 0.5f;
    lfo1.waveform(LFOWaveform::Sine);

    auto& lfo2 = chain.add<LFO>("lfo2");
    lfo2.frequency = 0.3f;
    lfo2.waveform(LFOWaveform::Triangle);

    // 1. SolidColor - Simple flat color (animated hue)
    auto& solid = chain.add<SolidColor>("solid");
    solid.color.set(1.0f, 0.5f, 0.2f, 1.0f);

    // 2. Gradient - Linear gradient between two colors
    auto& gradient = chain.add<Gradient>("gradient");
    gradient.colorA.set(1.0f, 0.2f, 0.5f, 1.0f);
    gradient.colorB.set(0.2f, 0.5f, 1.0f, 1.0f);
    gradient.angle = 0.785f;  // 45 degrees in radians

    // 3. Ramp - Animated HSV gradient
    auto& ramp = chain.add<Ramp>("ramp");
    ramp.hueSpeed = 0.2f;
    ramp.saturation = 0.8f;
    ramp.brightness = 0.9f;

    // 4. Shape - SDF circle
    auto& circle = chain.add<Shape>("circle");
    circle.type(ShapeType::Circle);
    circle.size.set(0.3f, 0.3f);
    circle.color.set(1.0f, 0.8f, 0.2f, 1.0f);
    circle.softness = 0.02f;

    // 5. Shape - SDF rectangle
    auto& rect = chain.add<Shape>("rect");
    rect.type(ShapeType::Rectangle);
    rect.size.set(0.4f, 0.3f);
    rect.color.set(0.2f, 1.0f, 0.6f, 1.0f);
    rect.cornerRadius = 0.05f;

    // 6. Shape - SDF polygon (star-like)
    auto& star = chain.add<Shape>("star");
    star.type(ShapeType::Polygon);
    star.sides = 5;
    star.size.set(0.35f, 0.35f);
    star.color.set(1.0f, 0.4f, 0.8f, 1.0f);

    // Create 2x3 grid using Canvas
    // NOTE: Canvas.drawImage() requires operators to be processed first.
    // Use input() to establish dependencies for correct processing order.
    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(ctx.width(), ctx.height());
    canvas.input(0, "solid");
    canvas.input(1, "gradient");
    canvas.input(2, "ramp");
    canvas.input(3, "circle");
    canvas.input(4, "rect");
    canvas.input(5, "star");

    chain.output("canvas");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    // Get LFO values
    auto& lfo1 = chain.get<LFO>("lfo1");
    auto& lfo2 = chain.get<LFO>("lfo2");
    float v1 = lfo1.value();
    float v2 = lfo2.value();

    // Animate solid color hue
    auto& solid = chain.get<SolidColor>("solid");
    float hue = std::fmod(ctx.time() * 0.1f, 1.0f);
    solid.color.set(
        0.5f + 0.5f * std::sin(hue * 6.28f),
        0.5f + 0.5f * std::sin(hue * 6.28f + 2.09f),
        0.5f + 0.5f * std::sin(hue * 6.28f + 4.18f),
        1.0f
    );

    // Animate gradient angle
    auto& gradient = chain.get<Gradient>("gradient");
    gradient.angle = v1 * 3.14159f;  // radians

    // Animate shape positions using LFO
    auto& circle = chain.get<Shape>("circle");
    circle.position.set(0.5f + v1 * 0.1f, 0.5f + v2 * 0.1f);

    auto& star = chain.get<Shape>("star");
    star.rotation = ctx.time() * 0.5f;  // radians

    // Draw 2x3 grid
    auto& canvas = chain.get<Canvas>("canvas");
    canvas.clear(0.1f, 0.1f, 0.12f, 1.0f);

    int w = ctx.width();
    int h = ctx.height();
    int cellW = w / 3;
    int cellH = h / 2;

    auto& ramp = chain.get<Ramp>("ramp");
    auto& rect = chain.get<Shape>("rect");

    // Row 1: SolidColor, Gradient, Ramp
    canvas.drawImage(solid, 0, 0, cellW, cellH);
    canvas.drawImage(gradient, cellW, 0, cellW, cellH);
    canvas.drawImage(ramp, cellW * 2, 0, cellW, cellH);

    // Row 2: Circle, Rectangle, Star
    canvas.drawImage(circle, 0, cellH, cellW, cellH);
    canvas.drawImage(rect, cellW, cellH, cellW, cellH);
    canvas.drawImage(star, cellW * 2, cellH, cellW, cellH);

    // Draw labels
    canvas.fillStyle(1.0f, 1.0f, 1.0f, 0.8f);
    canvas.fillText("SolidColor", 10, 25);
    canvas.fillText("Gradient", cellW + 10, 25);
    canvas.fillText("Ramp", cellW * 2 + 10, 25);
    canvas.fillText("Shape: Circle", 10, cellH + 25);
    canvas.fillText("Shape: Rectangle", cellW + 10, cellH + 25);
    canvas.fillText("Shape: Polygon", cellW * 2 + 10, cellH + 25);
}

VIVID_CHAIN(setup, update)
