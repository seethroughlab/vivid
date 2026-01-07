// Creative Effects Example
// Demonstrates: Pixelate, Plexus, FilmGrain, Flash
//
// Shows experimental and stylized visual effects

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Source: animated gradient for effects
    auto& source = chain.add<Ramp>("source");
    source.hueSpeed = 0.15f;

    // Pixelate - mosaic/retro effect
    auto& pixelate = chain.add<Pixelate>("pixelate");
    pixelate.input("source");
    pixelate.size.set(12.0f, 12.0f);

    // Plexus - particle network visualization
    auto& plexus = chain.add<Plexus>("plexus");
    plexus.setNodeCount(150);
    plexus.setNodeSize(0.006f);
    plexus.setNodeColor(0.3f, 0.8f, 1.0f, 0.9f);
    plexus.setConnectionDistance(0.12f);
    plexus.setLineColor(0.2f, 0.6f, 1.0f, 0.4f);
    plexus.setTurbulence(0.15f);
    plexus.setClearColor(0.02f, 0.02f, 0.05f, 1.0f);

    // FilmGrain - vintage film look
    auto& grain = chain.add<FilmGrain>("grain");
    grain.input("source");
    grain.intensity = 0.2f;
    grain.size = 1.2f;
    grain.speed = 24.0f;
    grain.colored = 0.2f;

    // Flash - triggered strobe effect
    auto& flash = chain.add<Flash>("flash");
    flash.input("source");
    flash.decay = 0.88f;
    flash.color.set(1.0f, 0.9f, 0.7f);
    flash.mode = 0;  // Additive

    // Canvas for grid layout
    // NOTE: Canvas.drawImage() requires operators to be processed first.
    // Use input() to establish dependencies for correct processing order.
    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(ctx.width(), ctx.height());
    canvas.input(0, "pixelate");
    canvas.input(1, "plexus");
    canvas.input(2, "grain");
    canvas.input(3, "flash");

    chain.output("canvas");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    // Animate pixelate size
    auto& pixelate = chain.get<Pixelate>("pixelate");
    float pixelSize = 8.0f + std::sin(t * 0.4f) * 6.0f;  // 2 to 14
    pixelate.size.set(pixelSize, pixelSize);

    // Update plexus
    auto& plexus = chain.get<Plexus>("plexus");
    plexus.setTurbulence(0.1f + std::sin(t * 0.2f) * 0.08f);
    plexus.setConnectionDistance(0.1f + std::sin(t * 0.3f) * 0.04f);

    // Animate film grain
    auto& grain = chain.get<FilmGrain>("grain");
    grain.intensity = 0.15f + std::sin(t * 0.5f) * 0.1f;  // 0.05 to 0.25

    // Auto-trigger flash periodically
    auto& flash = chain.get<Flash>("flash");
    static float lastTrigger = 0.0f;
    float triggerInterval = 1.5f + std::sin(t * 0.1f) * 0.5f;  // Variable rhythm
    if (t - lastTrigger > triggerInterval) {
        flash.trigger(0.8f);
        lastTrigger = t;
    }

    // Draw 2x2 grid
    auto& canvas = chain.get<Canvas>("canvas");
    canvas.clear(0.03f, 0.03f, 0.05f, 1.0f);

    int w = ctx.width();
    int h = ctx.height();
    int halfW = w / 2;
    int halfH = h / 2;
    int pad = 8;

    // Top-left: Pixelate
    canvas.drawImage(pixelate, pad, pad, halfW - pad * 2, halfH - pad * 2);

    // Top-right: Plexus
    canvas.drawImage(plexus, halfW + pad, pad, halfW - pad * 2, halfH - pad * 2);

    // Bottom-left: FilmGrain
    canvas.drawImage(grain, pad, halfH + pad, halfW - pad * 2, halfH - pad * 2);

    // Bottom-right: Flash
    canvas.drawImage(flash, halfW + pad, halfH + pad, halfW - pad * 2, halfH - pad * 2);

    // Labels
    canvas.fillStyle(0.0f, 0.0f, 0.0f, 0.7f);
    canvas.fillRect(pad, pad, 130, 22);
    canvas.fillRect(halfW + pad, pad, 140, 22);
    canvas.fillRect(pad, halfH + pad, 160, 22);
    canvas.fillRect(halfW + pad, halfH + pad, 140, 22);

    canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);

    char pixLabel[64];
    snprintf(pixLabel, sizeof(pixLabel), "Pixelate: %.0fpx", pixelSize);
    canvas.fillText(pixLabel, pad + 5, pad + 16);

    canvas.fillText("Plexus: 150 nodes", halfW + pad + 5, pad + 16);

    char grainLabel[64];
    snprintf(grainLabel, sizeof(grainLabel), "FilmGrain: int=%.2f",
        static_cast<float>(grain.intensity));
    canvas.fillText(grainLabel, pad + 5, halfH + pad + 16);

    char flashLabel[64];
    snprintf(flashLabel, sizeof(flashLabel), "Flash: %.0f%%",
        flash.intensity() * 100.0f);
    canvas.fillText(flashLabel, halfW + pad + 5, halfH + pad + 16);
}

VIVID_CHAIN(setup, update)
