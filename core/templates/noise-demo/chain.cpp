// Noise Demo - Vivid Project
// Animated noise with blur effect

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Simplex noise generator
    auto& noise = chain.add<Noise>("noise");
    noise.type(NoiseType::Simplex);
    noise.scale = 4.0f;
    noise.speed = 0.3f;
    noise.octaves = 4;

    // Gaussian blur
    auto& blur = chain.add<Blur>("blur");
    blur.input(&noise);
    blur.radius = 5.0f;

    // Color tint via HSV ramp
    auto& ramp = chain.add<Ramp>("ramp");
    ramp.type(RampType::Radial);
    ramp.hueSpeed = 0.1f;

    // Combine
    auto& comp = chain.add<Composite>("comp");
    comp.inputA(&blur);
    comp.inputB(&ramp);
    comp.mode(BlendMode::Multiply);

    chain.output("comp");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = static_cast<float>(ctx.time());

    // Animate noise
    chain.get<Noise>("noise").offset.set(time * 0.2f, time * 0.15f, 0.0f);

    // Pulse blur with time
    float pulse = 3.0f + 2.0f * std::sin(time);
    chain.get<Blur>("blur").radius = pulse;

    // Cycle hue
    chain.get<Ramp>("ramp").hueOffset = std::fmod(time * 0.05f, 1.0f);
}

VIVID_CHAIN(setup, update)
