// Chain Demo - Vivid Example
// Demonstrates the Chain API with image distortion and HSV color cycling
//
// Resolution handling:
// - Image: Uses loaded file's native resolution
// - Noise, Ramp: Generators use their declared resolution (or default 1280x720)
// - Displace, Composite: Processors inherit resolution from their input

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

    // Load an image - uses the file's native resolution
    image.file = "assets/images/nature.jpg";

    // Configure noise for displacement - use Simplex for smooth distortion
    // Note: Noise resolution should match the image for proper displacement
    // If not specified, generators default to 1280x720
    noise.type(NoiseType::Simplex);
    noise.scale = 3.0f;
    noise.speed = 0.3f;
    noise.octaves = 3;
    noise.lacunarity = 2.0f;
    noise.persistence = 0.5f;

    // Displace: processor that inherits resolution from its source input (image)
    displace.source("image");
    displace.map("noise");
    displace.strength = 0.08f;

    // HSV ramp for color tinting - generator with its own resolution
    ramp.type(RampType::Radial);
    ramp.hueSpeed = 0.1f;
    ramp.hueRange = 0.3f;
    ramp.saturation = 0.6f;
    ramp.brightness = 1.0f;

    // Composite: inherits resolution from first input (displace → image resolution)
    comp.inputA("displace");
    comp.inputB("ramp");
    comp.mode(BlendMode::Multiply);

    // Final output is scaled to window size for display
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
    noise.offset.set(time * 0.2f, time * 0.15f, 0.0f);

    // Mouse interaction:
    // X controls displacement strength (0.02 to 0.15)
    float strength = 0.02f + (ctx.mouseNorm().x * 0.5f + 0.5f) * 0.13f;
    displace.strength = strength;

    // Y controls color saturation
    float saturation = 0.3f + (ctx.mouseNorm().y * 0.5f + 0.5f) * 0.7f;
    ramp.saturation = saturation;

    // Slowly rotate hue offset
    ramp.hueOffset = std::fmod(time * 0.05f, 1.0f);

    // V key toggles vsync
    if (ctx.key(GLFW_KEY_V).pressed) {
        ctx.vsync(!ctx.vsync());
    }
}

VIVID_CHAIN(setup, update)
