// Kaleidoscope - Vivid Example
// Demonstrates symmetry and animation: Noise, Transform, Mirror, HSV, Bloom, ChromaticAberration

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Source: animated noise pattern
    auto& noise = chain.add<Noise>("noise");

    // Transform for rotation
    auto& transform = chain.add<Transform>("transform");

    // Kaleidoscope mirror effect
    auto& mirror = chain.add<Mirror>("mirror");

    // Color and post effects
    auto& hsv = chain.add<HSV>("hsv");
    auto& bloom = chain.add<Bloom>("bloom");
    auto& chromatic = chain.add<ChromaticAberration>("chromatic");

    // Configure noise: complex fractal pattern
    noise.type(NoiseType::Simplex);
    noise.scale = 2.5f;
    noise.speed = 0.4f;
    noise.octaves = 4;
    noise.lacunarity = 2.2f;
    noise.persistence = 0.55f;

    // Transform: will animate rotation
    transform.input("noise");

    // Mirror: kaleidoscope mode with 8 segments
    mirror.input("transform");
    mirror.mode(MirrorMode::Kaleidoscope);
    mirror.segments = 8;
    mirror.center.set(0.5f, 0.5f);

    // HSV: hue shift for color
    hsv.input("mirror");
    hsv.saturation = 0.7f;
    hsv.value = 1.0f;

    // Bloom: glow on bright areas
    bloom.input("hsv");
    bloom.threshold = 0.5f;
    bloom.intensity = 0.6f;
    bloom.radius = 8.0f;
    bloom.passes = 2;

    // Chromatic aberration for optical effect
    chromatic.input("bloom");
    chromatic.amount = 0.3f;
    chromatic.radial = true;

    chain.output("chromatic");

    if (chain.hasError()) {
        ctx.setError(chain.error());
    }
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = static_cast<float>(ctx.time());

    auto& noise = chain.get<Noise>("noise");
    auto& transform = chain.get<Transform>("transform");
    auto& mirror = chain.get<Mirror>("mirror");
    auto& hsv = chain.get<HSV>("hsv");
    auto& bloom = chain.get<Bloom>("bloom");

    // Animate noise offset for flowing pattern
    noise.offset.set(time * 0.3f, time * 0.2f, 0.0f);

    // Smooth rotation
    transform.rotation = time * 0.2f;
    float s = 1.0f + 0.1f * std::sin(time * 0.5f);
    transform.scale.set(s, s);

    // Animate kaleidoscope angle
    mirror.angle = time * 0.1f;

    // Mouse X controls hue
    float hue = (ctx.mouseNorm().x * 0.5f + 0.5f);
    hsv.hueShift = hue;

    // Mouse Y controls bloom intensity
    float bloomIntensity = 0.2f + (ctx.mouseNorm().y * 0.5f + 0.5f) * 0.8f;
    bloom.intensity = bloomIntensity;

    // Number keys change segment count (if held)
    for (int i = 3; i <= 12; i++) {
        if (ctx.key(GLFW_KEY_0 + (i % 10)).held) {
            mirror.segments = i;
        }
    }

    // Debug value monitoring - visible in the debug panel (D key)
    ctx.debug("rotation", transform.rotation);
    ctx.debug("scale", s);
    ctx.debug("hue", hue);
    ctx.debug("bloom", bloomIntensity);
    ctx.debug("segments", mirror.segments);
}

VIVID_CHAIN(setup, update)
