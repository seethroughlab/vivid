// Compositing Example
// Demonstrates: Composite (blend modes), Math, Logic
//
// Shows texture layering with different blend modes

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Layer A: Animated gradient
    auto& layerA = chain.add<Ramp>("layerA");
    layerA.hueSpeed = 0.1f;

    // Layer B: Noise texture
    auto& noise = chain.add<Noise>("noise");
    noise.scale = 4.0f;
    noise.octaves = 3;

    // Shape layer
    auto& shape = chain.add<Shape>("shape");
    shape.type(ShapeType::Circle);
    shape.size.set(0.35f, 0.35f);
    shape.color.set(1.0f, 1.0f, 1.0f, 0.8f);

    // Composite with Over mode (default)
    auto& compOver = chain.add<Composite>("compOver");
    compOver.inputA("layerA");
    compOver.inputB("shape");
    compOver.mode(BlendMode::Over);

    // Composite with Add mode
    auto& compAdd = chain.add<Composite>("compAdd");
    compAdd.inputA("layerA");
    compAdd.inputB("noise");
    compAdd.mode(BlendMode::Add);

    // Composite with Multiply mode
    auto& compMult = chain.add<Composite>("compMult");
    compMult.inputA("layerA");
    compMult.inputB("noise");
    compMult.mode(BlendMode::Multiply);

    // Composite with Screen mode
    auto& compScreen = chain.add<Composite>("compScreen");
    compScreen.inputA("layerA");
    compScreen.inputB("noise");
    compScreen.mode(BlendMode::Screen);

    // Math operator for value remapping
    auto& mathRemap = chain.add<Math>("mathRemap");
    mathRemap.operation(MathOperation::Remap);
    mathRemap.inMin = -1.0f;
    mathRemap.inMax = 1.0f;
    mathRemap.outMin = 0.2f;
    mathRemap.outMax = 0.8f;

    // Logic operator for conditional
    auto& logic = chain.add<Logic>("logic");
    logic.operation(LogicOperation::GreaterThan);
    logic.inputB = 0.5f;

    // Canvas for grid layout
    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(ctx.width(), ctx.height());

    chain.output("canvas");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    // Animate noise
    auto& noise = chain.get<Noise>("noise");
    noise.speed = 0.2f;

    // Animate shape position
    auto& shape = chain.get<Shape>("shape");
    shape.position.set(0.5f + std::sin(t * 0.5f) * 0.2f, 0.5f + std::cos(t * 0.7f) * 0.2f);

    // Animate composite opacities
    auto& compOver = chain.get<Composite>("compOver");
    compOver.opacity = 0.7f + std::sin(t * 0.3f) * 0.3f;

    // Math: remap a sine wave
    auto& mathRemap = chain.get<Math>("mathRemap");
    mathRemap.inputA = std::sin(t * 0.5f);  // -1 to 1
    float remappedValue = mathRemap.value();  // 0.2 to 0.8

    // Logic: check if remapped value is above threshold
    auto& logic = chain.get<Logic>("logic");
    logic.inputA = remappedValue;
    bool isAboveThreshold = logic.result();

    // Use logic result to control blend mode opacity
    auto& compAdd = chain.get<Composite>("compAdd");
    compAdd.opacity = isAboveThreshold ? 1.0f : 0.5f;

    // Draw 2x2 grid of blend modes
    auto& canvas = chain.get<Canvas>("canvas");
    canvas.clear(0.05f, 0.05f, 0.07f, 1.0f);

    int w = ctx.width();
    int h = ctx.height();
    int halfW = w / 2;
    int halfH = h / 2;
    int pad = 8;

    auto& compMult = chain.get<Composite>("compMult");
    auto& compScreen = chain.get<Composite>("compScreen");

    // Top-left: Over blend
    canvas.drawImage(compOver, pad, pad, halfW - pad * 2, halfH - pad * 2);

    // Top-right: Add blend
    canvas.drawImage(compAdd, halfW + pad, pad, halfW - pad * 2, halfH - pad * 2);

    // Bottom-left: Multiply blend
    canvas.drawImage(compMult, pad, halfH + pad, halfW - pad * 2, halfH - pad * 2);

    // Bottom-right: Screen blend
    canvas.drawImage(compScreen, halfW + pad, halfH + pad, halfW - pad * 2, halfH - pad * 2);

    // Labels
    canvas.fillStyle(0.0f, 0.0f, 0.0f, 0.7f);
    canvas.fillRect(pad, pad, 140, 22);
    canvas.fillRect(halfW + pad, pad, 180, 22);
    canvas.fillRect(pad, halfH + pad, 100, 22);
    canvas.fillRect(halfW + pad, halfH + pad, 90, 22);

    canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);

    char overLabel[64];
    snprintf(overLabel, sizeof(overLabel), "Over: opacity=%.2f",
        static_cast<float>(compOver.opacity));
    canvas.fillText(overLabel, pad + 5, pad + 16);

    char addLabel[80];
    snprintf(addLabel, sizeof(addLabel), "Add: logic=%s opacity=%.1f",
        isAboveThreshold ? "true" : "false", static_cast<float>(compAdd.opacity));
    canvas.fillText(addLabel, halfW + pad + 5, pad + 16);

    canvas.fillText("Multiply", pad + 5, halfH + pad + 16);
    canvas.fillText("Screen", halfW + pad + 5, halfH + pad + 16);

    // Show Math/Logic status in bottom area
    canvas.fillStyle(0.3f, 0.3f, 0.4f, 1.0f);
    canvas.fillRect(pad, h - 50, w - pad * 2, 42);

    canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);
    char mathLabel[128];
    snprintf(mathLabel, sizeof(mathLabel),
        "Math: sin(t)=%.2f -> Remap[0.2,0.8]=%.2f  |  Logic: %.2f > 0.5 = %s",
        std::sin(t * 0.5f), remappedValue, remappedValue, isAboveThreshold ? "true" : "false");
    canvas.fillText(mathLabel, pad + 10, h - 30);
}

VIVID_CHAIN(setup, update)
