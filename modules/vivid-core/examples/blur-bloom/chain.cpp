// Blur & Bloom Example
// Demonstrates: Blur, Bloom, Vignette
//
// Shows glow effects and cinematic post-processing

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Source: bright shapes on dark background (good for bloom)
    auto& bg = chain.add<SolidColor>("bg");
    bg.color.set(0.02f, 0.02f, 0.05f, 1.0f);

    auto& shape1 = chain.add<Shape>("shape1");
    shape1.type = ShapeType::Ellipse;
    shape1.size.set(0.15f, 0.15f);
    shape1.color.set(1.0f, 0.4f, 0.1f, 1.0f);  // Orange
    shape1.position.set(0.25f, 0.5f);  // Left of center

    auto& shape2 = chain.add<Shape>("shape2");
    shape2.type = ShapeType::Ellipse;
    shape2.size.set(0.12f, 0.12f);
    shape2.color.set(0.2f, 0.6f, 1.0f, 1.0f);  // Blue
    shape2.position.set(0.75f, 0.5f);  // Right of center

    auto& shape3 = chain.add<Shape>("shape3");
    shape3.type = ShapeType::Polygon;
    shape3.sides = 6;
    shape3.size.set(0.1f, 0.1f);
    shape3.color.set(0.4f, 1.0f, 0.4f, 1.0f);  // Green
    shape3.position.set(0.5f, 0.75f);  // Below center

    // Composite all shapes onto background (multi-input)
    auto& source = chain.add<Composite>("source");
    source.input(0, "bg");
    source.input(1, "shape1");
    source.input(2, "shape2");
    source.input(3, "shape3");
    source.mode = BlendMode::Add;

    // Blur - Gaussian blur effect
    auto& blur = chain.add<Blur>("blur");
    blur.input("source");
    blur.radius = 8.0f;
    blur.passes = 2;

    // Vignette - edge darkening
    auto& vignette = chain.add<Vignette>("vignette");
    vignette.input("source");
    vignette.intensity = 0.6f;
    vignette.softness = 0.5f;
    vignette.roundness = 1.0f;

    // Combined: Bloom -> Vignette (cinematic look)
    auto& combined_bloom = chain.add<Bloom>("combined_bloom");
    combined_bloom.input("source");

    auto& combined_vignette = chain.add<Vignette>("combined_vignette");
    combined_vignette.input("combined_bloom");

    // Canvas for grid layout
    // NOTE: Canvas.drawImage() requires operators to be processed first.
    // Use input() to establish dependencies for correct processing order.
    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(ctx.width(), ctx.height());
    canvas.input(0, "source");
    canvas.input(1, "blur");
    canvas.input(2, "vignette");
    canvas.input(3, "combined_vignette");

    chain.output("canvas");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    // Animate shapes for visual interest (positions in UV space: 0-1, center is 0.5)
    auto& shape1 = chain.get<Shape>("shape1");
    shape1.position.set(0.25f + std::sin(t * 0.5f) * 0.1f, 0.5f + std::sin(t * 0.7f) * 0.15f);

    auto& shape2 = chain.get<Shape>("shape2");
    shape2.position.set(0.75f + std::cos(t * 0.6f) * 0.1f, 0.5f + std::cos(t * 0.8f) * 0.15f);

    auto& shape3 = chain.get<Shape>("shape3");
    shape3.position.set(0.5f + std::sin(t * 0.4f) * 0.15f, 0.75f + std::cos(t * 0.5f) * 0.1f);
    shape3.rotation = t * 0.5f;  // radians

    // Animate blur
    auto& blur = chain.get<Blur>("blur");
    blur.radius = 5.0f + std::sin(t * 0.3f) * 4.0f;  // 1 to 9

    // Animate vignette
    auto& vignette = chain.get<Vignette>("vignette");
    vignette.intensity = 0.5f + std::sin(t * 0.25f) * 0.3f;  // 0.2 to 0.8
    vignette.softness = 0.4f + std::sin(t * 0.3f) * 0.2f;    // 0.2 to 0.6

    // Combined effect settings
    auto& combined_bloom = chain.get<Bloom>("combined_bloom");
    combined_bloom.threshold = 0.3f;
    combined_bloom.intensity = 1.8f;
    combined_bloom.radius = 20.0f;

    auto& combined_vignette = chain.get<Vignette>("combined_vignette");
    combined_vignette.intensity = 0.7f;
    combined_vignette.softness = 0.6f;

    // Draw 2x2 grid
    auto& canvas = chain.get<Canvas>("canvas");
    canvas.clear(0.0f, 0.0f, 0.0f, 1.0f);

    int w = ctx.width();
    int h = ctx.height();
    int halfW = w / 2;
    int halfH = h / 2;
    int pad = 8;

    auto& source = chain.get<Composite>("source");

    // Top-left: Original
    canvas.drawImage(source, pad, pad, halfW - pad * 2, halfH - pad * 2);

    // Top-right: Blur
    canvas.drawImage(blur, halfW + pad, pad, halfW - pad * 2, halfH - pad * 2);

    // Bottom-left: Vignette
    canvas.drawImage(vignette, pad, halfH + pad, halfW - pad * 2, halfH - pad * 2);

    // Bottom-right: Combined (Bloom + Vignette)
    canvas.drawImage(combined_vignette, halfW + pad, halfH + pad, halfW - pad * 2, halfH - pad * 2);

    // Labels
    canvas.fillStyle(0.0f, 0.0f, 0.0f, 0.7f);
    canvas.fillRect(pad, pad, 70, 22);
    canvas.fillRect(halfW + pad, pad, 140, 22);
    canvas.fillRect(pad, halfH + pad, 200, 22);
    canvas.fillRect(halfW + pad, halfH + pad, 180, 22);

    canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);
    auto fm = canvas.fontMetrics();
    float textY = pad + fm.ascent;
    float textY2 = halfH + pad + fm.ascent;
    canvas.fillText("Original", pad + 5, textY);

    char blurLabel[64];
    snprintf(blurLabel, sizeof(blurLabel), "Blur: radius=%.1f", static_cast<float>(blur.radius));
    canvas.fillText(blurLabel, halfW + pad + 5, textY);

    char vignetteLabel[64];
    snprintf(vignetteLabel, sizeof(vignetteLabel), "Vignette: int=%.2f soft=%.2f",
        static_cast<float>(vignette.intensity), static_cast<float>(vignette.softness));
    canvas.fillText(vignetteLabel, pad + 5, textY2);

    canvas.fillText("Combined: Bloom + Vignette", halfW + pad + 5, textY2);
}

VIVID_CHAIN(setup, update)
