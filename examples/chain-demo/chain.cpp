// Chain Demo - Vivid Example
// Demonstrates the Chain API with image distortion and HSV color cycling

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Add operators
    auto& image = chain.add<Image>("image");
    auto& noise = chain.add<Noise>("noise");
    auto& displace = chain.add<Displace>("displace");
    auto& ramp = chain.add<Ramp>("ramp");
    auto& comp = chain.add<Composite>("comp");

    // Load an image from assets
    image.file("assets/images/nature.jpg");

    // Configure noise for displacement - use Simplex for smooth distortion
    noise.type(NoiseType::Simplex)
        .scale(3.0f)
        .speed(0.3f)
        .octaves(3)
        .lacunarity(2.0f)
        .persistence(0.5f);

    // Configure displacement: image distorted by noise
    displace.source(&image)
        .map(&noise)
        .strength(0.08f);

    // Configure HSV ramp for color tinting
    ramp.type(RampType::Radial)
        .hueSpeed(0.1f)
        .hueRange(0.3f)
        .saturation(0.6f)
        .brightness(1.0f);

    // Multiply displaced image with color ramp for tinting effect
    comp.inputA(&displace).inputB(&ramp).mode(BlendMode::Multiply);

    // Specify output
    chain.output("comp");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = static_cast<float>(ctx.time());

    // Animate noise for flowing distortion
    auto& noise = chain.get<Noise>("noise");
    auto& displace = chain.get<Displace>("displace");
    auto& ramp = chain.get<Ramp>("ramp");

    // Drift noise offset for organic motion
    noise.offset(time * 0.2f, time * 0.15f);

    // Mouse interaction:
    // X controls displacement strength (0.02 to 0.15)
    float strength = 0.02f + (ctx.mouseNorm().x * 0.5f + 0.5f) * 0.13f;
    displace.strength(strength);

    // Y controls color saturation
    float saturation = 0.3f + (ctx.mouseNorm().y * 0.5f + 0.5f) * 0.7f;
    ramp.saturation(saturation);

    // Slowly rotate hue offset
    ramp.hueOffset(time * 0.05f);
}

VIVID_CHAIN(setup, update)
