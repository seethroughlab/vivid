// Parameter Modulation Example
// Demonstrates: Different binding methods for dynamic parameter control
//
// Shows 3 binding methods:
// 1. bind(operator, inMin, inMax, outMin, outMax) - Trackable operator binding
// 2. bind(lambda, outMin, outMax) - Lambda with range mapping
// 3. bindDirect(lambda) - Lambda with full control

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>
#include <algorithm>

using namespace vivid;
using namespace vivid::effects;

static Context* g_ctx = nullptr;

void setup(Context& ctx) {
    g_ctx = &ctx;
    auto& chain = ctx.chain();

    // Background gradient
    auto& bg = chain.add<Gradient>("bg");
    bg.colorA.set(0.08f, 0.08f, 0.12f, 1.0f);
    bg.colorB.set(0.12f, 0.1f, 0.15f, 1.0f);
    bg.angle = 45.0f;

    // LFO for position modulation
    auto& lfo = chain.add<LFO>("lfo");
    lfo.frequency = 0.5f;
    lfo.waveform = LFOWaveform::Sine;

    // === SHAPE 1: Operator binding (TRACKABLE) ===
    // bind(operator, inMin, inMax, outMin, outMax)
    // Shows as dashed orange line in chain visualizer
    auto& shape1 = chain.add<Shape>("shape1");
    shape1.type = ShapeType::Ellipse;
    shape1.size.set(0.12f, 0.12f);
    shape1.color.set(1.0f, 0.5f, 0.2f, 1.0f);  // Orange
    shape1.position.set(0.5f, 0.25f);
    // LFO outputs -1 to 1, map to X position 0.2 to 0.8
    shape1.position.bindX(lfo, -1.0f, 1.0f, 0.2f, 0.8f);

    // === SHAPE 2: Lambda with range mapping ===
    // bind(lambda, outMin, outMax)
    // Lambda must return 0-1, which is mapped to output range
    auto& shape2 = chain.add<Shape>("shape2");
    shape2.type = ShapeType::Rectangle;
    shape2.color.set(0.3f, 0.8f, 0.4f, 1.0f);  // Green
    shape2.position.set(0.5f, 0.5f);
    // Mouse X (0-1) maps to size 0.06-0.2
    shape2.size.bind(
        [&]() { return g_ctx->mouseNorm().x; },
        0.06f, 0.2f
    );

    // === SHAPE 3: Lambda direct binding ===
    // bindDirect(lambda)
    // Lambda returns the exact value - full control, no range mapping
    auto& shape3 = chain.add<Shape>("shape3");
    shape3.type = ShapeType::Polygon;
    shape3.sides = 6;
    shape3.size.set(0.1f, 0.1f);
    shape3.position.set(0.5f, 0.75f);
    shape3.color.set(0.8f, 0.4f, 0.9f, 1.0f);  // Purple
    // Rotation controlled directly by time - no range mapping needed
    shape3.rotation.bindDirect([&]() {
        return g_ctx->time() * 0.5f;  // Returns exact radians
    });

    // Canvas for drawing everything
    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(ctx.width(), ctx.height());
    canvas.input(0, "bg");
    canvas.input(1, "shape1");
    canvas.input(2, "shape2");
    canvas.input(3, "shape3");
    canvas.loadBuiltinFont(ctx, BuiltinFont::Mono, 14.0f);

    chain.output("canvas");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    int w = ctx.width();
    int h = ctx.height();

    // Get operators for value display
    auto& lfo = chain.get<LFO>("lfo");
    auto& shape1 = chain.get<Shape>("shape1");
    auto& shape2 = chain.get<Shape>("shape2");
    auto& shape3 = chain.get<Shape>("shape3");
    auto& bg = chain.get<Gradient>("bg");

    float lfoVal = lfo.value();
    float mouseX = ctx.mouseNorm().x;
    float t = ctx.time();
    float rotation = t * 0.5f;  // Same calc as bindDirect

    // Draw canvas
    auto& canvas = chain.get<Canvas>("canvas");
    canvas.clear(0, 0, 0, 0);

    // Draw background
    canvas.drawImage(bg, 0, 0, w, h);

    // Draw shapes
    canvas.drawImage(shape1, 0, 0, w, h);
    canvas.drawImage(shape2, 0, 0, w, h);
    canvas.drawImage(shape3, 0, 0, w, h);

    // === TEXT OVERLAY ===
    // Title
    canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);
    canvas.textAlign(TextAlign::Left);
    canvas.textBaseline(TextBaseline::Top);
    canvas.fillText("Parameter Modulation - Binding Methods", 20, 20);

    // Shape 1 label (trackable)
    float y1 = h * 0.25f;
    canvas.fillStyle(1.0f, 0.7f, 0.3f, 1.0f);  // Orange = trackable
    char buf[128];
    snprintf(buf, sizeof(buf), "bind(lfo, -1, 1, 0.2, 0.8)  x=%.2f  [TRACKABLE]",
        0.2f + (lfoVal + 1.0f) * 0.3f);
    canvas.fillText(buf, 20, y1 - 30);

    // Shape 2 label (lambda range)
    float y2 = h * 0.5f;
    canvas.fillStyle(0.5f, 0.9f, 1.0f, 1.0f);  // Cyan = lambda
    snprintf(buf, sizeof(buf), "bind(lambda, 0.06, 0.2)  size=%.2f",
        0.06f + mouseX * 0.14f);
    canvas.fillText(buf, 20, y2 - 30);

    // Shape 3 label (lambda direct)
    float y3 = h * 0.75f;
    canvas.fillStyle(0.5f, 0.9f, 1.0f, 1.0f);  // Cyan = lambda
    snprintf(buf, sizeof(buf), "bindDirect(lambda)  rotation=%.2f rad  (spinning)", rotation);
    canvas.fillText(buf, 20, y3 - 30);

    // Control hint
    canvas.fillStyle(0.5f, 0.5f, 0.5f, 1.0f);
    canvas.textBaseline(TextBaseline::Bottom);
    canvas.fillText("Move mouse horizontally to change square size", 20, h - 20);
}

VIVID_CHAIN(setup, update)
