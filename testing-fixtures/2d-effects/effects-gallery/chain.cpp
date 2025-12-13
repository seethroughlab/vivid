// 2D Effects Gallery - Tests many 2D effect operators
// Tests: Mirror, Edge, Dither, Pixelate, Quantize, ChromaticAberration, Scanlines, Vignette

#include <vivid/vivid.h>
#include <vivid/effects/noise.h>
#include <vivid/effects/hsv.h>
#include <vivid/effects/mirror.h>
#include <vivid/effects/edge.h>
#include <vivid/effects/dither.h>
#include <vivid/effects/pixelate.h>
#include <vivid/effects/quantize.h>
#include <vivid/effects/chromatic_aberration.h>
#include <vivid/effects/scanlines.h>
#include <vivid/effects/vignette.h>
#include <vivid/effects/composite.h>
#include <vivid/effects/transform.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Base texture: animated noise with color
    chain.add<Noise>("noise")
        .scale(3.0f)
        .speed(0.3f)
        .octaves(4);

    chain.add<HSV>("colorize")
        .input("noise")
        .hue(0.15f)
        .saturation(0.7f);

    // Row 1: Mirror effects (kaleidoscope)
    chain.add<Mirror>("mirror")
        .input("colorize")
        .mode(Mirror::Mode::Kaleidoscope)
        .segments(6);

    // Row 2: Edge detection
    chain.add<Edge>("edge")
        .input("colorize")
        .strength(1.0f);

    // Row 3: Dithering
    chain.add<Dither>("dither")
        .input("colorize")
        .pattern(Dither::Pattern::Bayer4x4)
        .levels(4);

    // Row 4: Pixelation
    chain.add<Pixelate>("pixelate")
        .input("colorize")
        .blockSize(8);

    // Row 5: Color quantization
    chain.add<Quantize>("quantize")
        .input("colorize")
        .levels(4);

    // Row 6: Chromatic aberration
    chain.add<ChromaticAberration>("chroma")
        .input("colorize")
        .amount(0.01f)
        .mode(ChromaticAberration::Mode::Radial);

    // Row 7: Scanlines
    chain.add<Scanlines>("scanlines")
        .input("colorize")
        .intensity(0.3f)
        .count(240);

    // Row 8: Vignette
    chain.add<Vignette>("vignette")
        .input("colorize")
        .radius(0.7f)
        .softness(0.4f);

    // Create a grid layout showing all effects
    // Scale each effect to fit in a 2x4 grid
    chain.add<Transform>("t_mirror").input("mirror").scale(0.5f).translate(-0.5f, 0.75f);
    chain.add<Transform>("t_edge").input("edge").scale(0.5f).translate(0.5f, 0.75f);
    chain.add<Transform>("t_dither").input("dither").scale(0.5f).translate(-0.5f, 0.25f);
    chain.add<Transform>("t_pixelate").input("pixelate").scale(0.5f).translate(0.5f, 0.25f);
    chain.add<Transform>("t_quantize").input("quantize").scale(0.5f).translate(-0.5f, -0.25f);
    chain.add<Transform>("t_chroma").input("chroma").scale(0.5f).translate(0.5f, -0.25f);
    chain.add<Transform>("t_scanlines").input("scanlines").scale(0.5f).translate(-0.5f, -0.75f);
    chain.add<Transform>("t_vignette").input("vignette").scale(0.5f).translate(0.5f, -0.75f);

    // Composite all transformed effects
    chain.add<Composite>("final")
        .input(0, "t_mirror")
        .input(1, "t_edge")
        .input(2, "t_dither")
        .input(3, "t_pixelate")
        .input(4, "t_quantize")
        .input(5, "t_chroma")
        .input(6, "t_scanlines")
        .input(7, "t_vignette")
        .mode(Composite::Mode::Add);

    chain.output("final");
}

void update(Context& ctx) {
    // Animation driven by Noise speed parameter
}

VIVID_CHAIN(setup, update)
