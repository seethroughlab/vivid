// Testing Fixture: Blend Modes Demo
// Tests all Composite blend modes with animated gradients
//
// Use keyboard 1-6 to cycle through blend modes:
// 1=Over, 2=Add, 3=Multiply, 4=Screen, 5=Overlay, 6=Difference

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

// Track current blend mode
static int currentMode = 0;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Layer A: Warm animated gradient
    auto& layerA = chain.add<Gradient>("layerA");
    layerA.mode(GradientMode::Linear);
    layerA.colorA.set(0.9f, 0.3f, 0.1f, 1.0f);
    layerA.colorB.set(1.0f, 0.8f, 0.2f, 1.0f);

    // Layer B: Cool animated gradient with transparency
    auto& layerB = chain.add<Gradient>("layerB");
    layerB.mode(GradientMode::Linear);
    layerB.colorA.set(0.1f, 0.3f, 0.9f, 0.8f);
    layerB.colorB.set(0.5f, 0.1f, 0.8f, 0.8f);

    // Composite with blend mode
    auto& comp = chain.add<Composite>("comp");
    comp.inputA(&layerA);
    comp.inputB(&layerB);
    comp.mode(BlendMode::Over);

    chain.output("comp");

    if (chain.hasError()) {
        ctx.setError(chain.error());
    }
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = static_cast<float>(ctx.time());

    // Animate gradient angles
    auto& layerA = chain.get<Gradient>("layerA");
    layerA.angle = t * 20.0f;

    auto& layerB = chain.get<Gradient>("layerB");
    layerB.angle = 90.0f + t * 30.0f;

    // Keyboard to change blend mode
    if (ctx.key(GLFW_KEY_1).pressed) currentMode = 0;
    if (ctx.key(GLFW_KEY_2).pressed) currentMode = 1;
    if (ctx.key(GLFW_KEY_3).pressed) currentMode = 2;
    if (ctx.key(GLFW_KEY_4).pressed) currentMode = 3;
    if (ctx.key(GLFW_KEY_5).pressed) currentMode = 4;
    if (ctx.key(GLFW_KEY_6).pressed) currentMode = 5;

    // Apply blend mode
    auto& comp = chain.get<Composite>("comp");
    switch (currentMode) {
        case 0: comp.mode(BlendMode::Over); break;
        case 1: comp.mode(BlendMode::Add); break;
        case 2: comp.mode(BlendMode::Multiply); break;
        case 3: comp.mode(BlendMode::Screen); break;
        case 4: comp.mode(BlendMode::Overlay); break;
        case 5: comp.mode(BlendMode::Difference); break;
    }
}

VIVID_CHAIN(setup, update)
