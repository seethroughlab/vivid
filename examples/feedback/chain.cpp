// Feedback Demo - Vivid V3 Example
// Demonstrates feedback trails with state preservation across hot-reload

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

static Chain* chain = nullptr;

void setup(Context& ctx) {
    chain = new Chain();

    // Add operators
    auto& noise = chain->add<Noise>("noise");
    auto& feedback = chain->add<Feedback>("feedback");
    auto& ramp = chain->add<Ramp>("ramp");
    auto& comp = chain->add<Composite>("comp");
    auto& output = chain->add<Output>("output");

    // Configure noise - small bright spots
    noise.scale(8.0f).speed(0.8f).octaves(2);

    // Configure feedback - trails with slight zoom and rotation
    feedback.input(&noise)
        .decay(0.92f)       // 8% decay per frame - long trails
        .mix(0.3f)          // 30% new input, 70% feedback
        .zoom(1.002f)       // Slight zoom out for spiral effect
        .rotate(0.005f);    // Slight rotation per frame

    // Configure HSV ramp for colorization
    ramp.type(RampType::Radial)
        .hueSpeed(0.1f)
        .hueRange(0.5f)
        .saturation(0.9f)
        .brightness(1.0f);

    // Multiply feedback trails with color ramp
    comp.inputA(&feedback).inputB(&ramp).mode(BlendMode::Multiply);

    output.input(&comp);
    chain->setOutput("output");
    chain->init(ctx);

    // Restore state from previous hot-reload (preserves feedback buffer)
    ctx.restoreStates(*chain);

    if (chain->hasError()) {
        ctx.setError(chain->error());
    }
}

void update(Context& ctx) {
    if (!chain) return;

    float time = static_cast<float>(ctx.time());

    // Animate noise offset for drifting particles
    auto& noise = chain->get<Noise>("noise");
    noise.offset(time * 0.5f, time * 0.3f);

    // Mouse controls feedback parameters
    auto& feedback = chain->get<Feedback>("feedback");

    // X: rotation speed (-0.02 to 0.02)
    float rotation = (ctx.mouseNorm().x) * 0.02f;
    feedback.rotate(rotation);

    // Y: decay (0.85 to 0.98)
    float decay = 0.85f + ctx.mouseNorm().y * 0.065f + 0.065f;
    feedback.decay(decay);

    // Animate ramp hue offset
    auto& ramp = chain->get<Ramp>("ramp");
    ramp.hueOffset(time * 0.05f);

    chain->process(ctx);
}

extern "C" {
    void vivid_setup(Context& ctx) {
        // Save state before destroying old chain
        if (chain) {
            ctx.preserveStates(*chain);
        }

        delete chain;
        chain = nullptr;
        setup(ctx);
    }

    void vivid_update(Context& ctx) {
        update(ctx);
    }
}
