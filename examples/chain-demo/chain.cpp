// Chain Demo - Vivid V3 Example
// Demonstrates the Chain API with animated HSV color cycling

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

// Chain object (persistent across hot-reloads)
static Chain* chain = nullptr;

void setup(Context& ctx) {
    // Create chain
    chain = new Chain();

    // Add operators with names
    auto& noise1 = chain->add<Noise>("noise1");
    auto& noise2 = chain->add<Noise>("noise2");
    auto& ramp = chain->add<Ramp>("ramp");
    auto& comp1 = chain->add<Composite>("comp1");
    auto& comp2 = chain->add<Composite>("comp2");
    auto& output = chain->add<Output>("output");

    // Configure noise layers - animated and flowing
    noise1.scale(3.0f).speed(0.4f).octaves(5).lacunarity(2.2f).persistence(0.55f);
    noise2.scale(6.0f).speed(0.7f).octaves(3).offset(50.0f, 25.0f);

    // Configure HSV ramp - radial rainbow cycling
    ramp.type(RampType::Angular)
        .hueSpeed(0.15f)     // Slow rotation through hues
        .hueRange(1.0f)      // Full spectrum
        .saturation(0.85f)   // Rich colors
        .brightness(1.0f)
        .repeat(2.0f);       // Two color cycles

    // Build dependency graph:
    // noise1 * noise2 -> comp1 (multiply creates organic texture)
    // comp1 * ramp -> comp2 (colorize with HSV gradient)
    // comp2 -> output
    comp1.inputA(&noise1).inputB(&noise2).mode(BlendMode::Multiply);
    comp2.inputA(&comp1).inputB(&ramp).mode(BlendMode::Multiply);
    output.input(&comp2);

    // Set output operator
    chain->setOutput("output");

    // Initialize chain (computes execution order)
    chain->init(ctx);

    if (chain->hasError()) {
        ctx.setError(chain->error());
    }
}

void update(Context& ctx) {
    if (!chain) return;

    float time = static_cast<float>(ctx.time());

    // Animate noise parameters for flowing organic motion
    auto& noise1 = chain->get<Noise>("noise1");
    auto& noise2 = chain->get<Noise>("noise2");
    auto& ramp = chain->get<Ramp>("ramp");

    // Noise 1: Scale pulses slowly, offset drifts
    float scale1 = 3.0f + sin(time * 0.3f) * 0.5f;
    noise1.scale(scale1).offset(time * 0.1f, time * 0.05f);

    // Noise 2: Counter-drift for more complex motion
    noise2.offset(50.0f - time * 0.15f, 25.0f + time * 0.08f);

    // Ramp: Rotate the gradient angle for swirling effect
    float angle = time * 0.2f;
    ramp.angle(angle);

    // Mouse interaction: X controls hue speed, Y controls saturation
    float hueSpeed = 0.1f + ctx.mouseNorm().x * 0.3f;
    float saturation = 0.5f + ctx.mouseNorm().y * 0.5f;
    ramp.hueSpeed(hueSpeed).saturation(saturation);

    // Process entire chain
    chain->process(ctx);
}

// Export entry points
extern "C" {
    void vivid_setup(Context& ctx) {
        // Clean up previous chain if hot-reloading
        delete chain;
        chain = nullptr;

        setup(ctx);
    }

    void vivid_update(Context& ctx) {
        update(ctx);
    }
}
