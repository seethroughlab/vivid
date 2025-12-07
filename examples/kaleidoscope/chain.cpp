// Kaleidoscope - Vivid Example
// Demonstrates symmetry and animation: Noise, Transform, Mirror, HSV, Bloom, ChromaticAberration

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

static Chain* chain = nullptr;

void setup(Context& ctx) {
    delete chain;
    chain = new Chain();

    // Source: animated noise pattern
    auto& noise = chain->add<Noise>("noise");

    // Transform for rotation
    auto& transform = chain->add<Transform>("transform");

    // Kaleidoscope mirror effect
    auto& mirror = chain->add<Mirror>("mirror");

    // Color and post effects
    auto& hsv = chain->add<HSV>("hsv");
    auto& bloom = chain->add<Bloom>("bloom");
    auto& chromatic = chain->add<ChromaticAberration>("chromatic");
    auto& output = chain->add<Output>("output");

    // Configure noise: complex fractal pattern
    noise.type(NoiseType::Simplex)
        .scale(2.5f)
        .speed(0.4f)
        .octaves(4)
        .lacunarity(2.2f)
        .persistence(0.55f);

    // Transform: will animate rotation
    transform.input(&noise);

    // Mirror: kaleidoscope mode with 8 segments
    mirror.input(&transform)
        .mode(MirrorMode::Kaleidoscope)
        .segments(8)
        .center(0.5f, 0.5f);

    // HSV: hue shift for color
    hsv.input(&mirror)
        .saturation(0.7f)
        .value(1.0f);

    // Bloom: glow on bright areas
    bloom.input(&hsv)
        .threshold(0.5f)
        .intensity(0.6f)
        .radius(8.0f)
        .passes(2);

    // Chromatic aberration for optical effect
    chromatic.input(&bloom)
        .amount(0.3f)
        .radial(true);

    output.input(&chromatic);
    chain->setOutput("output");
    chain->init(ctx);

    if (chain->hasError()) {
        ctx.setError(chain->error());
    }
}

void update(Context& ctx) {
    if (!chain) return;

    float time = static_cast<float>(ctx.time());

    auto& noise = chain->get<Noise>("noise");
    auto& transform = chain->get<Transform>("transform");
    auto& mirror = chain->get<Mirror>("mirror");
    auto& hsv = chain->get<HSV>("hsv");
    auto& bloom = chain->get<Bloom>("bloom");

    // Animate noise offset for flowing pattern
    noise.offset(time * 0.3f, time * 0.2f);

    // Smooth rotation
    transform.rotate(time * 0.2f)
        .scale(1.0f + 0.1f * std::sin(time * 0.5f));

    // Animate kaleidoscope angle
    mirror.angle(time * 0.1f);

    // Mouse X controls hue
    float hue = (ctx.mouseNorm().x * 0.5f + 0.5f);
    hsv.hueShift(hue);

    // Mouse Y controls bloom intensity
    float bloomIntensity = 0.2f + (ctx.mouseNorm().y * 0.5f + 0.5f) * 0.8f;
    bloom.intensity(bloomIntensity);

    // Number keys change segment count (if held)
    for (int i = 3; i <= 12; i++) {
        if (ctx.key(GLFW_KEY_0 + (i % 10)).held) {
            mirror.segments(i);
        }
    }

    chain->process(ctx);
}

VIVID_CHAIN(setup, update)
