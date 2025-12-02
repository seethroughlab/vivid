// Spout Output Example
// Shares Vivid output with other applications via Spout (Windows only)
//
// To receive this in another app:
// - TouchDesigner: Use a Syphon Spout In TOP
// - Resolume: Add a Spout source
// - OBS: Use the Spout2 plugin
// - Any Spout-compatible application

#include <vivid/vivid.h>
#include <vivid/spout/spout.h>
#include <iostream>

using namespace vivid;

// Spout sender (global for persistence across hot-reload)
static std::unique_ptr<spout::Sender> spoutSender;

void setup(Chain& chain) {
    // Create animated visual content
    chain.add<Noise>("noise")
        .scale(4.0f)
        .speed(1.0f)
        .octaves(4);

    chain.add<HSV>("color")
        .input("noise")
        .saturation(1.5f)
        .brightness(1.0f);

    chain.setOutput("color");

    std::cout << "\n=== Spout Output Example ===\n";
    std::cout << "Sharing texture via Spout as 'Vivid'\n";
    std::cout << "Connect from TouchDesigner, Resolume, OBS, etc.\n";
    std::cout << "Press S to check sender status\n\n";
}

static int frameCount = 0;

void update(Chain& chain, Context& ctx) {
    // Animate colors
    float hue = std::fmod(ctx.time() * 0.1f, 1.0f);
    chain.get<HSV>("color").hueShift(hue);

    frameCount++;

    // Create Spout sender after a few frames (let things stabilize)
    if (frameCount == 10 && !spoutSender) {
        std::cout << "[Spout] Creating sender...\n";
        try {
            spoutSender = std::make_unique<spout::Sender>("Vivid");
            if (!spoutSender->valid()) {
                std::cerr << "[Spout] Failed to create sender (invalid)\n";
                spoutSender.reset();
            } else {
                std::cout << "[Spout] Sender created successfully\n";
            }
        } catch (const std::exception& e) {
            std::cerr << "[Spout] Exception creating sender: " << e.what() << "\n";
        }
    }

    // Get output texture and send to Spout
    if (spoutSender && spoutSender->valid()) {
        Texture* output = chain.getOutput(ctx);
        if (output && output->valid()) {
            spoutSender->sendFrame(*output, ctx);
        }
    }

    // Show status on S key
    if (ctx.wasKeyPressed(Key::S)) {
        if (spoutSender && spoutSender->hasReceivers()) {
            std::cout << "[Spout] Sender active: " << spoutSender->name() << "\n";
        } else {
            std::cout << "[Spout] Sender not active or no receivers\n";
        }
    }

    // Window management keys
    if (ctx.wasKeyPressed(Key::F)) ctx.toggleFullscreen();
    if (ctx.wasKeyPressed(Key::Escape)) ctx.setFullscreen(false);
}

VIVID_CHAIN(setup, update)
