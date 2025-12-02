// Syphon Output Example
// Shares Vivid output with other applications via Syphon (macOS only)
//
// To receive this in another app:
// - TouchDesigner: Use a Syphon Spout In TOP
// - Resolume: Add a Syphon source
// - VDMX: Add a Syphon source
// - Simple Syphon Client: https://github.com/Syphon/Simple

#include <vivid/vivid.h>
#include <vivid/syphon/syphon.h>
#include <iostream>

using namespace vivid;

// Syphon server (global for persistence across hot-reload)
static std::unique_ptr<syphon::Server> syphonServer;

void setup(Chain& chain) {
    // Create animated visual content
    chain.add<Noise>("noise")
        .scale(3.0f)
        .speed(0.5f)
        .octaves(3);

    chain.add<HSV>("color")
        .input("noise")
        .saturation(1.5f);

    chain.add<Bloom>("bloom")
        .input("color")
        .intensity(0.3f)
        .threshold(0.6f);

    chain.setOutput("bloom");

    std::cout << "\n=== Syphon Output Example ===\n";
    std::cout << "Sharing texture via Syphon as 'Vivid'\n";
    std::cout << "Connect from TouchDesigner, Resolume, VDMX, etc.\n\n";
}

void update(Chain& chain, Context& ctx) {
    // Create Syphon server on first frame
    if (!syphonServer) {
        syphonServer = std::make_unique<syphon::Server>("Vivid");
        if (!syphonServer->valid()) {
            std::cerr << "[Syphon] Failed to create server\n";
            syphonServer.reset();
        }
    }

    // Animate colors
    float hue = std::fmod(ctx.time() * 0.1f, 1.0f);
    chain.get<HSV>("color").hueShift(hue);

    // Get output texture and publish to Syphon
    if (syphonServer && syphonServer->valid()) {
        Texture* output = chain.getOutput(ctx);
        if (output && output->valid()) {
            syphonServer->publishFrame(*output, ctx);
        }
    }

    // Show status
    if (ctx.wasKeyPressed(Key::S)) {
        if (syphonServer && syphonServer->hasClients()) {
            std::cout << "[Syphon] Clients connected\n";
        } else {
            std::cout << "[Syphon] No clients connected\n";
        }
    }
}

VIVID_CHAIN(setup, update)
