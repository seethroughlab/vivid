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
    auto& noise = chain.add<Noise>("noise");
    noise.scale = 3.0f;
    noise.speed = 0.3f;
    noise.octaves = 4;

    auto& colorize = chain.add<HSV>("colorize");
    colorize.input(&noise);
    colorize.hueShift(0.15f).saturation(0.7f);

    // Row 1: Mirror effects (kaleidoscope)
    auto& mirror = chain.add<Mirror>("mirror");
    mirror.input(&colorize);
    mirror.mode(MirrorMode::Kaleidoscope).segments(6);

    // Row 2: Edge detection
    auto& edge = chain.add<Edge>("edge");
    edge.input(&colorize);
    edge.strength(1.0f);

    // Row 3: Dithering
    auto& dither = chain.add<Dither>("dither");
    dither.input(&colorize);
    dither.pattern(DitherPattern::Bayer4x4).levels(4);

    // Row 4: Pixelation
    auto& pixelate = chain.add<Pixelate>("pixelate");
    pixelate.input(&colorize);
    pixelate.size(8.0f);

    // Row 5: Color quantization
    auto& quantize = chain.add<Quantize>("quantize");
    quantize.input(&colorize);
    quantize.levels(4);

    // Row 6: Chromatic aberration
    auto& chroma = chain.add<ChromaticAberration>("chroma");
    chroma.input(&colorize);
    chroma.amount(0.01f).radial(true);

    // Row 7: Scanlines
    auto& scanlines = chain.add<Scanlines>("scanlines");
    scanlines.input(&colorize);
    scanlines.intensity(0.3f).spacing(2);

    // Row 8: Vignette
    auto& vignette = chain.add<Vignette>("vignette");
    vignette.input(&colorize);
    vignette.intensity(0.7f).softness(0.4f);

    // Create a grid layout showing all effects
    auto& t_mirror = chain.add<Transform>("t_mirror");
    t_mirror.input(&mirror).scale(0.5f).translate(-0.5f, 0.75f);

    auto& t_edge = chain.add<Transform>("t_edge");
    t_edge.input(&edge).scale(0.5f).translate(0.5f, 0.75f);

    auto& t_dither = chain.add<Transform>("t_dither");
    t_dither.input(&dither).scale(0.5f).translate(-0.5f, 0.25f);

    auto& t_pixelate = chain.add<Transform>("t_pixelate");
    t_pixelate.input(&pixelate).scale(0.5f).translate(0.5f, 0.25f);

    auto& t_quantize = chain.add<Transform>("t_quantize");
    t_quantize.input(&quantize).scale(0.5f).translate(-0.5f, -0.25f);

    auto& t_chroma = chain.add<Transform>("t_chroma");
    t_chroma.input(&chroma).scale(0.5f).translate(0.5f, -0.25f);

    auto& t_scanlines = chain.add<Transform>("t_scanlines");
    t_scanlines.input(&scanlines).scale(0.5f).translate(-0.5f, -0.75f);

    auto& t_vignette = chain.add<Transform>("t_vignette");
    t_vignette.input(&vignette).scale(0.5f).translate(0.5f, -0.75f);

    // Composite all transformed effects
    auto& final = chain.add<Composite>("final");
    final.input(0, &t_mirror)
         .input(1, &t_edge)
         .input(2, &t_dither)
         .input(3, &t_pixelate)
         .input(4, &t_quantize)
         .input(5, &t_chroma)
         .input(6, &t_scanlines)
         .input(7, &t_vignette)
         .mode(BlendMode::Add);

    chain.output("final");
}

void update(Context& ctx) {
    // Animation driven by Noise speed parameter
}

VIVID_CHAIN(setup, update)
