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
    shape.type(ShapeType::Star);
    shape.sides = 5;
    shape.size.set(0.4f, 0.4f);
    shape.softness = 0.01f;
    shape.color.set(1.0f, 1.0f, 1.0f, 1.0f);  // White

    // Radial gradient background
    gradient.mode(GradientMode::Radial);
    gradient.colorA.set(0.1f, 0.0f, 0.2f, 1.0f);   // Dark purple center
    gradient.colorB.set(0.0f, 0.0f, 0.05f, 1.0f);  // Nearly black edge

    // Composite shape over gradient
    comp.inputA(&gradient);
    comp.inputB(&shape);
    comp.mode(BlendMode::Add);

    // HSV for hue cycling
    hsv.input(&comp);

    // Retro pipeline: downsample to low-res
    downsample.input(&hsv);
    downsample.targetW = 320;
    downsample.targetH = 240;

    // Dither for retro color banding
    dither.input(&downsample);
    dither.pattern(DitherPattern::Bayer4x4);
    dither.levels = 16;
    dither.strength = 0.8f;

    // CRT scanlines
    scanlines.input(&dither);
    scanlines.spacing = 3;
    scanlines.thickness = 0.4f;
    scanlines.intensity = 0.25f;

    // Full CRT effect
    crt.input(&scanlines);
    crt.curvature = 0.15f;
    crt.vignette = 0.4f;
    crt.scanlines = 0.1f;
    crt.bloom = 0.15f;
    crt.chromatic = 0.03f;

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
    shape.size.set(pulse, pulse);
    shape.rotation = time * 0.5f;

    // Cycle hue over time
    hsv.hueShift = std::fmod(time * 0.1f, 1.0f);

    // Mouse controls CRT parameters
    // X: curvature (0.0 to 0.3)
    float curvature = (ctx.mouseNorm().x * 0.5f + 0.5f) * 0.3f;
    // Y: chromatic aberration (0.0 to 0.05)
    float chromatic = (ctx.mouseNorm().y * 0.5f + 0.5f) * 0.05f;

    crt.curvature = curvature;
    crt.chromatic = chromatic;
}

VIVID_CHAIN(setup, update)
