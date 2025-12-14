// Feedback Demo - Vivid Example
// Demonstrates feedback trails with state preservation across hot-reload

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Add operators
    auto& noise = chain.add<Noise>("noise");
    auto& feedback = chain.add<Feedback>("feedback");
    auto& ramp = chain.add<Ramp>("ramp");
    auto& comp = chain.add<Composite>("comp");

    // Configure noise - small bright spots
    noise.scale = 8.0f;
    noise.speed = 0.8f;
    noise.octaves = 2;

    // Configure feedback - trails with slight zoom and rotation
    feedback.input(&noise);
    feedback.decay = 0.92f;       // 8% decay per frame - long trails
    feedback.mix = 0.3f;          // 30% new input, 70% feedback
    feedback.zoom = 1.002f;       // Slight zoom out for spiral effect
    feedback.rotate = 0.005f;     // Slight rotation per frame

    // Configure HSV ramp for colorization
    ramp.type(RampType::Radial);
    ramp.hueSpeed = 0.1f;
    ramp.hueRange = 0.5f;
    ramp.saturation = 0.9f;
    ramp.brightness = 1.0f;

    // Multiply feedback trails with color ramp
    comp.inputA(&feedback).inputB(&ramp).mode(BlendMode::Multiply);

    chain.output("comp");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = static_cast<float>(ctx.time());

    // Animate noise offset for drifting particles
    auto& noise = chain.get<Noise>("noise");
    noise.offset.set(time * 0.5f, time * 0.3f, 0.0f);

    // Mouse controls feedback parameters
    auto& feedback = chain.get<Feedback>("feedback");

    // X: rotation speed (-0.02 to 0.02)
    float rotation = (ctx.mouseNorm().x) * 0.02f;
    feedback.rotate = rotation;

    // Y: decay (0.85 to 0.98)
    float decay = 0.85f + ctx.mouseNorm().y * 0.065f + 0.065f;
    feedback.decay = decay;

    // Animate ramp hue offset
    auto& ramp = chain.get<Ramp>("ramp");
    ramp.hueOffset = std::fmod(time * 0.05f, 1.0f);
}

VIVID_CHAIN(setup, update)
