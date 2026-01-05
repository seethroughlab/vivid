// Feedback Loop - Vivid Project
// Classic video feedback effect with trails

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Noise source
    auto& noise = chain.add<Noise>("noise");
    noise.scale = 8.0f;
    noise.speed = 0.8f;
    noise.octaves = 2;

    // Feedback with trails
    auto& feedback = chain.add<Feedback>("feedback");
    feedback.input("noise");
    feedback.decay = 0.92f;
    feedback.mix = 0.3f;
    feedback.zoom = 1.002f;
    feedback.rotate = 0.005f;

    // Color ramp
    auto& ramp = chain.add<Ramp>("ramp");
    ramp.type(RampType::Radial);
    ramp.hueSpeed = 0.1f;
    ramp.saturation = 0.9f;

    // Multiply for color
    auto& comp = chain.add<Composite>("comp");
    comp.inputA("feedback");
    comp.inputB("ramp");
    comp.mode(BlendMode::Multiply);

    chain.output("comp");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = static_cast<float>(ctx.time());

    // Animate noise
    chain.get<Noise>("noise").offset.set(time * 0.5f, time * 0.3f, 0.0f);

    // Mouse controls feedback rotation
    float rotation = ctx.mouseNorm().x * 0.02f;
    chain.get<Feedback>("feedback").rotate = rotation;

    // Animate ramp hue
    chain.get<Ramp>("ramp").hueOffset = std::fmod(time * 0.05f, 1.0f);
}

VIVID_CHAIN(setup, update)
