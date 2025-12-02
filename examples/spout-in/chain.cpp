// Spout Input Example
// Receives textures from other applications via Spout (Windows only)
//
// To send to this from another app:
// - TouchDesigner: Use a Spout Out TOP
// - Resolume: Enable Spout output
// - OBS: Use the Spout2 plugin for output
// - Any Spout-compatible application

#include <vivid/vivid.h>
#include <vivid/spout/spout.h>
#include <iostream>

using namespace vivid;

// Spout receiver (global for persistence across hot-reload)
static std::unique_ptr<spout::Receiver> spoutReceiver;
static Texture receivedTexture;
static int frameCount = 0;

void setup(Chain& chain) {
    // Create a fallback visual when no Spout input is available
    chain.add<Noise>("fallback")
        .scale(4.0f)
        .speed(0.5f)
        .octaves(3);

    chain.add<HSV>("color")
        .input("fallback")
        .saturation(0.5f)
        .brightness(0.3f);

    chain.setOutput("color");

    std::cout << "\n=== Spout Input Example ===\n";
    std::cout << "Receiving textures via Spout\n";
    std::cout << "Send from TouchDesigner, Resolume, OBS, etc.\n";
    std::cout << "\nKeys:\n";
    std::cout << "  L - List available Spout senders\n";
    std::cout << "  R - Reconnect to first available sender\n\n";

    // List available senders at startup
    spout::Receiver::printSenders();
}

void update(Chain& chain, Context& ctx) {
    frameCount++;

    // Animate fallback
    float hue = std::fmod(ctx.time() * 0.05f, 1.0f);
    chain.get<HSV>("color").hueShift(hue);

    // Create Spout receiver after a few frames
    if (frameCount == 10 && !spoutReceiver) {
        std::cout << "[Spout] Creating receiver...\n";
        spoutReceiver = std::make_unique<spout::Receiver>();
        if (!spoutReceiver->valid()) {
            std::cerr << "[Spout] Failed to create receiver\n";
            spoutReceiver.reset();
        } else {
            std::cout << "[Spout] Receiver ready, waiting for senders...\n";
        }
    }

    // Handle keyboard input
    if (ctx.wasKeyPressed(Key::L)) {
        spout::Receiver::printSenders();
    }

    if (ctx.wasKeyPressed(Key::R)) {
        // Recreate receiver to connect to any available sender
        spoutReceiver = std::make_unique<spout::Receiver>();
        std::cout << "[Spout] Reconnecting...\n";
    }

    // Try to receive from Spout
    if (spoutReceiver && spoutReceiver->valid()) {
        if (spoutReceiver->receiveFrame(receivedTexture, ctx)) {
            // Successfully received a frame
            // Could use receivedTexture for further processing
            int w, h;
            spoutReceiver->getFrameSize(w, h);

            // Make fallback brighter when receiving
            chain.get<HSV>("color").brightness(1.0f);
        } else {
            // Not receiving, dim the fallback
            chain.get<HSV>("color").brightness(0.3f);
        }
    }

    // Window management keys
    if (ctx.wasKeyPressed(Key::F)) ctx.toggleFullscreen();
    if (ctx.wasKeyPressed(Key::Escape)) ctx.setFullscreen(false);
}

VIVID_CHAIN(setup, update)
