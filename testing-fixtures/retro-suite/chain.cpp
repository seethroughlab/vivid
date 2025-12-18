// Testing Fixture: Retro Effects Suite
// Tests retro/vintage visual effects in sequence
//
// Visual verification:
// - Animated star shape with full retro pipeline
// - Downsample → Dither → Scanlines → CRT

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Animated star shape
    auto& shape = chain.add<Shape>("shape");
    shape.type(ShapeType::Star);
    shape.sides = 5;
    shape.size.set(0.4f, 0.4f);
    shape.softness = 0.01f;
    shape.color.set(1.0f, 1.0f, 1.0f, 1.0f);

    // Radial gradient background
    auto& gradient = chain.add<Gradient>("gradient");
    gradient.mode(GradientMode::Radial);
    gradient.colorA.set(0.2f, 0.0f, 0.4f, 1.0f);
    gradient.colorB.set(0.0f, 0.0f, 0.1f, 1.0f);

    // Composite shape over gradient
    auto& comp = chain.add<Composite>("comp");
    comp.inputA(&gradient);
    comp.inputB(&shape);
    comp.mode(BlendMode::Add);

    // HSV for hue cycling
    auto& hsv = chain.add<HSV>("hsv");
    hsv.input(&comp);

    // Downsample to low-res
    auto& downsample = chain.add<Downsample>("downsample");
    downsample.input(&hsv);
    downsample.targetW = 320;
    downsample.targetH = 240;

    // Dither for retro color banding
    auto& dither = chain.add<Dither>("dither");
    dither.input(&downsample);
    dither.pattern(DitherPattern::Bayer4x4);
    dither.levels = 16;
    dither.strength = 0.8f;

    // CRT scanlines
    auto& scanlines = chain.add<Scanlines>("scanlines");
    scanlines.input(&dither);
    scanlines.spacing = 3;
    scanlines.thickness = 0.4f;
    scanlines.intensity = 0.25f;

    // Full CRT effect
    auto& crt = chain.add<CRTEffect>("crt");
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
    float t = static_cast<float>(ctx.time());

    // Animate shape
    auto& shape = chain.get<Shape>("shape");
    float pulse = 0.3f + 0.15f * std::sin(t * 2.0f);
    shape.size.set(pulse, pulse);
    shape.rotation = t * 0.5f;

    // Cycle hue
    auto& hsv = chain.get<HSV>("hsv");
    hsv.hueShift = std::fmod(t * 0.1f, 1.0f);

    // Mouse controls CRT
    auto& crt = chain.get<CRTEffect>("crt");
    crt.curvature = (ctx.mouseNorm().x * 0.5f + 0.5f) * 0.3f;
    crt.chromatic = (ctx.mouseNorm().y * 0.5f + 0.5f) * 0.05f;
}

VIVID_CHAIN(setup, update)
