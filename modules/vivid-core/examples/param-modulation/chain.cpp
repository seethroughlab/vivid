// Parameter Modulation Example
// Demonstrates: Trackable operator bindings for dynamic parameter control
//
// Shows how to use bind(Operator&) to modulate parameters with LFO values.
// Operator bindings are visualized as dashed orange lines in the chain visualizer.

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

// Store context reference for lambda captures (mouse-based bindings only)
static Context* g_ctx = nullptr;

void setup(Context& ctx) {
    g_ctx = &ctx;
    auto& chain = ctx.chain();

    // LFO for modulation source (outputs -1 to 1)
    auto& lfo = chain.add<LFO>("lfo");
    lfo.frequency = 0.5f;
    lfo.waveform = LFOWaveform::Sine;

    // Shape 1: Scale modulated by mouse X position
    // Mouse bindings still use lambda (no trackable source)
    auto& shape1 = chain.add<Shape>("shape1");
    shape1.type = ShapeType::Circle;
    shape1.color.set(1.0f, 0.4f, 0.2f, 1.0f);
    shape1.position.set(-0.3f, 0.2f);
    // Bind size to mouse X (normalized 0-1 mapped to 0.05-0.3)
    shape1.size.bind(
        [&]() {
            // mouseNorm returns 0 to 1, use directly
            return g_ctx->mouseNorm().x;
        },
        0.05f, 0.3f  // Output range
    );

    // Shape 2: X position modulated by LFO (TRACKABLE - shows in visualizer!)
    auto& shape2 = chain.add<Shape>("shape2");
    shape2.type = ShapeType::Rectangle;
    shape2.size.set(0.15f, 0.15f);
    shape2.color.set(0.2f, 0.8f, 0.4f, 1.0f);
    // Bind X position to LFO: input -1..1 maps to output -0.3..0.3
    // This creates a dashed orange connection line in the chain visualizer
    shape2.position.bindX(lfo, -1.0f, 1.0f, -0.3f, 0.3f);
    shape2.position.set(0.0f, -0.2f);  // Set Y position

    // Shape 3: Color will be modulated by time in update()
    auto& shape3 = chain.add<Shape>("shape3");
    shape3.type = ShapeType::Polygon;
    shape3.sides = 6;
    shape3.size.set(0.12f, 0.12f);
    shape3.position.set(0.3f, 0.0f);

    // Noise with scale modulated by LFO (TRACKABLE - shows in visualizer!)
    auto& noise = chain.add<Noise>("noise");
    noise.octaves = 3;
    // Bind scale: LFO -1..1 maps to scale 2.0..8.0
    // This creates a dashed orange connection line in the chain visualizer
    noise.scale.bind(lfo, -1.0f, 1.0f, 2.0f, 8.0f);

    // Background
    auto& bg = chain.add<SolidColor>("bg");
    bg.color.set(0.08f, 0.08f, 0.12f, 1.0f);

    // Composite layers
    auto& comp1 = chain.add<Composite>("comp1");
    comp1.inputA("bg");
    comp1.inputB("noise");
    comp1.mode = BlendMode::Add;
    comp1.opacity = 0.3f;

    auto& comp2 = chain.add<Composite>("comp2");
    comp2.inputA("comp1");
    comp2.inputB("shape1");
    comp2.mode = BlendMode::Over;

    auto& comp3 = chain.add<Composite>("comp3");
    comp3.inputA("comp2");
    comp3.inputB("shape2");
    comp3.mode = BlendMode::Over;

    auto& comp4 = chain.add<Composite>("comp4");
    comp4.inputA("comp3");
    comp4.inputB("shape3");
    comp4.mode = BlendMode::Over;

    // Canvas for UI
    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(ctx.width(), ctx.height());
    canvas.input(0, "comp4");

    chain.output("canvas");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    // Param bindings with bind(Operator&) evaluate automatically!
    // shape2.position.x and noise.scale are both auto-updated from LFO

    // Get LFO value for display (not needed for bound params)
    auto& lfo = chain.get<LFO>("lfo");
    float lfoVal = lfo.value();  // -1 to 1

    // Shape 3: Color modulated by time (hue cycling)
    auto& shape3 = chain.get<Shape>("shape3");
    float t = ctx.time();
    float hue = std::fmod(t * 0.2f, 1.0f);
    // Simple HSV to RGB (hue only, full saturation)
    float r = std::abs(hue * 6.0f - 3.0f) - 1.0f;
    float g = 2.0f - std::abs(hue * 6.0f - 2.0f);
    float b = 2.0f - std::abs(hue * 6.0f - 4.0f);
    shape3.color.set(
        std::max(0.0f, std::min(1.0f, r)),
        std::max(0.0f, std::min(1.0f, g)),
        std::max(0.0f, std::min(1.0f, b)),
        1.0f
    );

    // Draw UI overlay
    auto& canvas = chain.get<Canvas>("canvas");
    auto& comp4 = chain.get<Composite>("comp4");
    canvas.clear(0, 0, 0, 0);
    canvas.drawImage(comp4, 0, 0, ctx.width(), ctx.height());

    // Info panel
    canvas.fillStyle(0.0f, 0.0f, 0.0f, 0.75f);
    canvas.fillRect(10, 10, 440, 200);

    canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);
    canvas.fillText("Parameter Modulation Demo", 20, 35);

    canvas.fillStyle(1.0f, 0.7f, 0.3f, 1.0f);  // Orange for trackable
    canvas.fillText("Trackable Bindings (shown as dashed lines):", 20, 60);

    canvas.fillStyle(0.9f, 0.9f, 0.9f, 1.0f);
    canvas.fillText("  shape2.position.x <- LFO (-0.3 to 0.3)", 20, 80);
    canvas.fillText("  noise.scale <- LFO (2.0 to 8.0)", 20, 100);

    canvas.fillStyle(0.7f, 0.9f, 1.0f, 1.0f);
    canvas.fillText("Lambda Bindings (not trackable):", 20, 125);

    canvas.fillStyle(0.9f, 0.9f, 0.9f, 1.0f);
    canvas.fillText("  shape1.size <- mouse.x (0.05 to 0.3)", 20, 145);
    canvas.fillText("  shape3.color <- time (hue cycling)", 20, 165);

    // Show current values
    char valInfo[128];
    snprintf(valInfo, sizeof(valInfo), "LFO: %.2f  |  Mouse X: %.2f",
        lfoVal, ctx.mouseNorm().x);
    canvas.fillStyle(0.8f, 0.8f, 0.5f, 1.0f);
    canvas.fillText(valInfo, 20, 185);
}

VIVID_CHAIN(setup, update)
