// Testing Fixture: Particle System
// Tests PointSprites for GPU-accelerated particles
//
// Visual verification:
// - Animated point sprite field
// - Color gradient and size variation
// - Movement patterns

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Dark background
    auto& bg = chain.add<Gradient>("bg");
    bg.mode(GradientMode::Radial);
    bg.colorA.set(0.05f, 0.05f, 0.1f, 1.0f);
    bg.colorB.set(0.02f, 0.02f, 0.04f, 1.0f);

    // GPU point sprites
    auto& points = chain.add<PointSprites>("points");
    points.setCount(5000);
    points.setSize(4.0f);
    points.setSizeVariation(0.5f);
    points.setColor(0.2f, 0.5f, 1.0f, 1.0f);
    points.setColor2(1.0f, 0.3f, 0.8f, 1.0f);
    points.setAnimate(true);
    points.setAnimateSpeed(0.2f);

    // Composite with additive blending
    auto& comp = chain.add<Composite>("comp");
    comp.inputA(&bg);
    comp.inputB(&points);
    comp.mode(BlendMode::Add);

    // Add bloom for glow effect
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input(&comp);
    bloom.threshold = 0.3f;
    bloom.intensity = 0.6f;
    bloom.radius = 8.0f;

    chain.output("bloom");

    if (chain.hasError()) {
        ctx.setError(chain.error());
    }
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = static_cast<float>(ctx.time());

    auto& points = chain.get<PointSprites>("points");

    // Animate size with pulse
    points.setPulseSize(true);
    points.setPulseSpeed(2.0f);

    // Mouse controls
    float speed = 0.1f + (ctx.mouseNorm().x * 0.5f + 0.5f) * 0.4f;
    points.setAnimateSpeed(speed);
}

VIVID_CHAIN(setup, update)
