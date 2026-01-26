// Lesson 03: Parameters
// Explore interactive sliders and the Claude MCP workflow
//
// Run: ./build/bin/vivid projects/getting-started/03-parameters
//
// Press Tab to see sliders - adjust them, then ask Claude to update your code!

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Noise generator - try adjusting these with sliders!
    auto& noise = chain.add<Noise>("noise");
    noise.scale = 4.0f;     // Slider: pattern size
    noise.speed = 0.3f;     // Slider: animation speed
    noise.octaves = 3;      // Slider: detail layers

    // Blur - smooth out the noise
    auto& blur = chain.add<Blur>("blur");
    blur.input("noise");
    blur.radius = 8.0f;     // Slider: blur amount

    // Color adjustment
    auto& hsv = chain.add<HSV>("hsv");
    hsv.input("blur");
    hsv.hueShift = 0.0f;    // Slider: shift colors (0-1)
    hsv.saturation = 1.2f;  // Slider: color intensity
    hsv.value = 1.0f;       // Slider: brightness

    // Add some glow
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("hsv");
    bloom.threshold = 0.5f; // Slider: glow threshold
    bloom.intensity = 0.3f; // Slider: glow strength

    chain.output("bloom");
}

void update(Context& ctx) {
    // Called every frame - add interactive behavior here
}

VIVID_CHAIN(setup, update)
