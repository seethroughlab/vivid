// Retro CRT - Vivid Example
// Demonstrates retro effects: Shape, Downsample, Dither, Scanlines, CRTEffect

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Generators
    auto& shape = chain.add<Shape>("shape");
    auto& gradient = chain.add<Gradient>("gradient");
    auto& comp = chain.add<Composite>("comp");

    // Color and retro effects
    auto& hsv = chain.add<HSV>("hsv");
    auto& downsample = chain.add<Downsample>("downsample");
    auto& dither = chain.add<Dither>("dither");
    auto& scanlines = chain.add<Scanlines>("scanlines");
    auto& crt = chain.add<CRTEffect>("crt");

    // Animated star shape
    shape.type(ShapeType::Star)
        .sides(5)
        .size(0.4f, 0.4f)
        .softness(0.01f)
        .color(Color::White);

    // Radial gradient background
    gradient.mode(GradientMode::Radial)
        .colorA(Color::fromHex("#1A0033"))  // Dark purple center
        .colorB(Color::fromHex("#00000D")); // Nearly black edge

    // Composite shape over gradient
    comp.inputA(&gradient).inputB(&shape).mode(BlendMode::Add);

    // HSV for hue cycling
    hsv.input(&comp);

    // Retro pipeline: downsample to low-res
    downsample.input(&hsv)
        .resolution(320, 240)
        .filter(FilterMode::Nearest);

    // Dither for retro color banding
    dither.input(&downsample)
        .pattern(DitherPattern::Bayer4x4)
        .levels(16)
        .strength(0.8f);

    // CRT scanlines
    scanlines.input(&dither)
        .spacing(3)
        .thickness(0.4f)
        .intensity(0.25f);

    // Full CRT effect
    crt.input(&scanlines)
        .curvature(0.15f)
        .vignette(0.4f)
        .scanlines(0.1f)
        .bloom(0.15f)
        .chromatic(0.3f);

    chain.output("crt");

    if (chain.hasError()) {
        ctx.setError(chain.error());
    }
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = static_cast<float>(ctx.time());

    auto& shape = chain.get<Shape>("shape");
    auto& hsv = chain.get<HSV>("hsv");
    auto& crt = chain.get<CRTEffect>("crt");

    // Animate shape: pulsing size and rotation
    float pulse = 0.3f + 0.15f * std::sin(time * 2.0f);
    shape.size(pulse)
        .rotation(time * 0.5f);

    // Cycle hue over time
    hsv.hueShift(std::fmod(time * 0.1f, 1.0f));

    // Mouse controls CRT parameters
    // X: curvature (0.0 to 0.3)
    float curvature = (ctx.mouseNorm().x * 0.5f + 0.5f) * 0.3f;
    // Y: chromatic aberration (0.0 to 0.5)
    float chromatic = (ctx.mouseNorm().y * 0.5f + 0.5f) * 0.5f;

    crt.curvature(curvature)
        .chromatic(chromatic);
}

VIVID_CHAIN(setup, update)
