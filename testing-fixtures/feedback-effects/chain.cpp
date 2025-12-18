// Testing Fixture: Feedback Effects
// Tests temporal effects: Feedback with various configurations
//
// Visual verification:
// - Feedback trail with zoom and rotation creating spiral patterns
// - Noise particles leave colorful trails

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Animated noise source
    auto& noise = chain.add<Noise>("noise");
    noise.scale = 6.0f;
    noise.speed = 0.8f;
    noise.octaves = 2;

    // Feedback with zoom and rotation for spiral trails
    auto& feedback = chain.add<Feedback>("feedback");
    feedback.input(&noise);
    feedback.decay = 0.92f;
    feedback.mix = 0.4f;
    feedback.zoom = 1.003f;
    feedback.rotate = 0.01f;

    // Color ramp for colorization
    auto& ramp = chain.add<Ramp>("ramp");
    ramp.type(RampType::Radial);
    ramp.hueSpeed = 0.15f;
    ramp.hueRange = 0.6f;
    ramp.saturation = 0.9f;

    // Multiply feedback with color
    auto& comp = chain.add<Composite>("comp");
    comp.inputA(&feedback);
    comp.inputB(&ramp);
    comp.mode(BlendMode::Multiply);

    chain.output("comp");

    if (chain.hasError()) {
        ctx.setError(chain.error());
    }
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = static_cast<float>(ctx.time());

    // Animate noise offset for drifting particles
    auto& noise = chain.get<Noise>("noise");
    noise.offset.set(t * 0.5f, t * 0.3f, 0.0f);

    // Animate ramp hue
    auto& ramp = chain.get<Ramp>("ramp");
    ramp.hueOffset = std::fmod(t * 0.05f, 1.0f);

    // Mouse controls
    auto& feedback = chain.get<Feedback>("feedback");
    feedback.rotate = ctx.mouseNorm().x * 0.02f;
    feedback.decay = 0.85f + (ctx.mouseNorm().y * 0.5f + 0.5f) * 0.1f;
}

VIVID_CHAIN(setup, update)
