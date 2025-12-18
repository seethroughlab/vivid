// Testing Fixture: OSC Receiver
// Tests OscIn operator for receiving OSC messages
//
// Hardware requirement: External OSC source (TouchOSC, Max/MSP, etc.)
// Default port: 8000
//
// Expected OSC messages:
// - /vivid/color [r, g, b] - Set shape color
// - /vivid/x [x] - Set shape X position (0-1)
// - /vivid/y [y] - Set shape Y position (0-1)
// - /vivid/size [s] - Set shape size (0-1)

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // OSC Input
    auto& osc = chain.add<OscIn>("osc");
    osc.port(8000);

    // Background
    auto& bg = chain.add<Gradient>("bg");
    bg.mode(GradientMode::Radial);
    bg.colorA.set(0.1f, 0.1f, 0.2f, 1.0f);
    bg.colorB.set(0.05f, 0.05f, 0.1f, 1.0f);

    // Reactive shape
    auto& shape = chain.add<Shape>("shape");
    shape.type(ShapeType::Circle);
    shape.size.set(0.2f, 0.2f);
    shape.color.set(1.0f, 0.5f, 0.2f, 1.0f);
    shape.softness = 0.1f;

    // Composite
    auto& comp = chain.add<Composite>("comp");
    comp.inputA(&bg);
    comp.inputB(&shape);
    comp.mode(BlendMode::Add);

    // Bloom for glow
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input(&comp);
    bloom.threshold = 0.4f;
    bloom.intensity = 0.5f;

    chain.output("bloom");

    if (chain.hasError()) {
        ctx.setError(chain.error());
    }
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = static_cast<float>(ctx.time());

    auto& osc = chain.get<OscIn>("osc");
    auto& shape = chain.get<Shape>("shape");

    // Default animation when no OSC input
    float defaultX = 0.5f + std::sin(t * 0.5f) * 0.3f;
    float defaultY = 0.5f + std::cos(t * 0.7f) * 0.3f;

    // Check for OSC messages
    float x = osc.get("/vivid/x", defaultX);
    float y = osc.get("/vivid/y", defaultY);
    float size = osc.get("/vivid/size", 0.2f);

    shape.position.set(x, y);
    shape.size.set(size, size);

    // Color from OSC (with default)
    float r = osc.get("/vivid/color/r", 1.0f);
    float g = osc.get("/vivid/color/g", 0.5f);
    float b = osc.get("/vivid/color/b", 0.2f);
    shape.color.set(r, g, b, 1.0f);
}

VIVID_CHAIN(setup, update)
