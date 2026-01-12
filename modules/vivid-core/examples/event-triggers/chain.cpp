// Event Triggers Example
// Demonstrates: Trigger operator for event-driven effects
//
// Shows how to use Trigger operators to convert discrete events
// into smooth decay envelopes for driving visual parameters.
//
// Press keys to fire different triggers and see the visual response.

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <GLFW/glfw3.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Create multiple triggers with different decay rates
    // Fast decay - punchy, immediate response (like a kick drum)
    auto& fastTrigger = chain.add<Trigger>("fast");
    fastTrigger.decay = 0.85f;

    // Medium decay - balanced response (like a snare)
    auto& medTrigger = chain.add<Trigger>("medium");
    medTrigger.decay = 0.92f;

    // Slow decay - sustained response (like a pad or cymbal)
    auto& slowTrigger = chain.add<Trigger>("slow");
    slowTrigger.decay = 0.97f;

    // Attack trigger - demonstrates attack parameter
    auto& attackTrigger = chain.add<Trigger>("attack");
    attackTrigger.attack = 0.5f;  // Slow ramp-up
    attackTrigger.decay = 0.95f;

    // Visual elements
    auto& noise = chain.add<Noise>("noise");
    noise.scale = 4.0f;
    noise.speed = 0.3f;
    noise.octaves = 3;

    auto& hsv = chain.add<HSV>("hsv");
    hsv.input("noise");

    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("hsv");
    bloom.threshold = 0.4f;
    bloom.radius = 15.0f;

    // Shapes for each trigger visualization
    auto& shape1 = chain.add<Shape>("shape1");
    shape1.type = ShapeType::Ellipse;
    shape1.position.set(-0.3f, 0.0f);
    shape1.color.set(1.0f, 0.3f, 0.3f, 1.0f);

    auto& shape2 = chain.add<Shape>("shape2");
    shape2.type = ShapeType::Ellipse;
    shape2.position.set(0.0f, 0.0f);
    shape2.color.set(0.3f, 1.0f, 0.3f, 1.0f);

    auto& shape3 = chain.add<Shape>("shape3");
    shape3.type = ShapeType::Ellipse;
    shape3.position.set(0.3f, 0.0f);
    shape3.color.set(0.3f, 0.3f, 1.0f, 1.0f);

    // Canvas for compositing and UI
    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(ctx.width(), ctx.height());

    chain.output("canvas");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    // Get triggers
    auto& fastTrigger = chain.get<Trigger>("fast");
    auto& medTrigger = chain.get<Trigger>("medium");
    auto& slowTrigger = chain.get<Trigger>("slow");
    auto& attackTrigger = chain.get<Trigger>("attack");

    // Fire triggers from keyboard
    // 1, 2, 3 = fast, medium, slow with full intensity
    if (ctx.key(GLFW_KEY_1).pressed) fastTrigger.fire();
    if (ctx.key(GLFW_KEY_2).pressed) medTrigger.fire();
    if (ctx.key(GLFW_KEY_3).pressed) slowTrigger.fire();

    // 4 = attack trigger (slow ramp-up)
    if (ctx.key(GLFW_KEY_4).pressed) attackTrigger.fire();

    // Q, W, E = fire with varying intensities
    if (ctx.key(GLFW_KEY_Q).pressed) fastTrigger.fire(0.3f);
    if (ctx.key(GLFW_KEY_W).pressed) fastTrigger.fire(0.6f);
    if (ctx.key(GLFW_KEY_E).pressed) fastTrigger.fire(1.0f);

    // Space = fire all at once
    if (ctx.key(GLFW_KEY_SPACE).pressed) {
        fastTrigger.fire();
        medTrigger.fire();
        slowTrigger.fire();
    }

    // R = reset all triggers
    if (ctx.key(GLFW_KEY_R).pressed) {
        fastTrigger.reset();
        medTrigger.reset();
        slowTrigger.reset();
        attackTrigger.reset();
    }

    // Use trigger values to drive visual parameters
    auto& noise = chain.get<Noise>("noise");
    noise.scale = 4.0f + fastTrigger.value() * 8.0f;

    auto& hsv = chain.get<HSV>("hsv");
    hsv.hueShift = medTrigger.value() * 0.5f;
    hsv.saturation = 0.5f + slowTrigger.value() * 0.5f;

    auto& bloom = chain.get<Bloom>("bloom");
    bloom.intensity = 0.5f + attackTrigger.value() * 2.0f;

    // Update shapes based on trigger values
    auto& shape1 = chain.get<Shape>("shape1");
    shape1.size.set(0.05f + fastTrigger.value() * 0.15f,
                    0.05f + fastTrigger.value() * 0.15f);
    shape1.color.set(1.0f, 0.3f, 0.3f, fastTrigger.value());

    auto& shape2 = chain.get<Shape>("shape2");
    shape2.size.set(0.05f + medTrigger.value() * 0.15f,
                    0.05f + medTrigger.value() * 0.15f);
    shape2.color.set(0.3f, 1.0f, 0.3f, medTrigger.value());

    auto& shape3 = chain.get<Shape>("shape3");
    shape3.size.set(0.05f + slowTrigger.value() * 0.15f,
                    0.05f + slowTrigger.value() * 0.15f);
    shape3.color.set(0.3f, 0.3f, 1.0f, slowTrigger.value());

    chain.process(ctx);

    // Draw UI overlay
    auto& canvas = chain.get<Canvas>("canvas");
    canvas.clear(0, 0, 0, 0);  // Transparent

    // Draw bloom as background
    canvas.drawImage(bloom, 0, 0, static_cast<float>(ctx.width()), static_cast<float>(ctx.height()));

    // Draw trigger visualization shapes
    float w = static_cast<float>(ctx.width());
    float h = static_cast<float>(ctx.height());
    canvas.drawImage(shape1, 0, 0, w, h);
    canvas.drawImage(shape2, 0, 0, w, h);
    canvas.drawImage(shape3, 0, 0, w, h);

    // Draw labels and values
    canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);
    canvas.fillText("Event Triggers Demo", 20, 30);

    canvas.fillStyle(0.7f, 0.7f, 0.7f, 1.0f);
    canvas.fillText("Press 1/2/3 for fast/medium/slow triggers", 20, 60);
    canvas.fillText("Press 4 for attack trigger (slow ramp-up)", 20, 80);
    canvas.fillText("Press Q/W/E for low/med/high intensity", 20, 100);
    canvas.fillText("Press SPACE to fire all, R to reset", 20, 120);

    // Show trigger values
    char buf[128];

    canvas.fillStyle(1.0f, 0.3f, 0.3f, 1.0f);
    snprintf(buf, sizeof(buf), "Fast (decay=0.85): %.2f %s",
             fastTrigger.value(), fastTrigger.active() ? "[ACTIVE]" : "");
    canvas.fillText(buf, 20, 160);

    canvas.fillStyle(0.3f, 1.0f, 0.3f, 1.0f);
    snprintf(buf, sizeof(buf), "Medium (decay=0.92): %.2f %s",
             medTrigger.value(), medTrigger.active() ? "[ACTIVE]" : "");
    canvas.fillText(buf, 20, 180);

    canvas.fillStyle(0.3f, 0.3f, 1.0f, 1.0f);
    snprintf(buf, sizeof(buf), "Slow (decay=0.97): %.2f %s",
             slowTrigger.value(), slowTrigger.active() ? "[ACTIVE]" : "");
    canvas.fillText(buf, 20, 200);

    canvas.fillStyle(1.0f, 1.0f, 0.3f, 1.0f);
    snprintf(buf, sizeof(buf), "Attack (attack=0.5): %.2f %s",
             attackTrigger.value(), attackTrigger.active() ? "[ACTIVE]" : "");
    canvas.fillText(buf, 20, 220);

    chain.output("canvas");
}

VIVID_CHAIN(setup, update)
