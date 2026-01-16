// Texture Sharing Example
// Demonstrates sharing textures between Vivid and other applications
//
// This example creates an animated noise pattern and shares it via
// Syphon (macOS) or Spout (Windows). Other applications like Resolume,
// VDMX, TouchDesigner, or another Vivid instance can receive the texture.
//
// Controls:
//   1: List available servers
//   2: Toggle sharing on/off
//   R: Reset noise animation

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/texshare/texshare.h>
#include <iostream>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::texshare;

static bool sharingEnabled = true;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // Generator - Animated Noise
    // =========================================================================

    auto& noise = chain.add<Noise>("noise");
    noise.scale = 3.0f;
    noise.speed = 0.5f;
    noise.octaves = 4;
    noise.type = NoiseType::Simplex;

    // =========================================================================
    // Color Grading
    // =========================================================================

    auto& hueShift = chain.add<HueShift>("hue");
    hueShift.input("noise");
    hueShift.amount = 0.5f;

    // =========================================================================
    // Texture Sharing Output
    // =========================================================================

    auto& share = chain.add<TextureShareOut>("share");
    share.input("hue");
    share.serverName = "Vivid Demo";

    // =========================================================================
    // Output
    // =========================================================================

    chain.output("share");

    std::cout << "[texture-sharing] Sharing as 'Vivid Demo'" << std::endl;
    std::cout << "[texture-sharing] Press 1 to list available servers" << std::endl;
    std::cout << "[texture-sharing] Press 2 to toggle sharing" << std::endl;
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // Input Handling
    // =========================================================================

    // Key 1: List available servers (for receiving)
    if (ctx.key(GLFW_KEY_1).pressed) {
        auto& share = chain.get<TextureShareOut>("share");
        // Create a temporary receiver to list servers
        TextureShareIn tempReceiver;
        auto servers = tempReceiver.availableServers();

        std::cout << "[texture-sharing] Available servers:" << std::endl;
        if (servers.empty()) {
            std::cout << "  (none found)" << std::endl;
        } else {
            for (const auto& server : servers) {
                std::cout << "  - " << server.name;
                if (!server.appName.empty()) {
                    std::cout << " (from " << server.appName << ")";
                }
                std::cout << std::endl;
            }
        }
    }

    // Key 2: Toggle sharing
    if (ctx.key(GLFW_KEY_2).pressed) {
        sharingEnabled = !sharingEnabled;
        std::cout << "[texture-sharing] Sharing " << (sharingEnabled ? "enabled" : "disabled") << std::endl;
    }

    // Key R: Reset animation
    if (ctx.key(GLFW_KEY_R).pressed) {
        ctx.resetTime();
        std::cout << "[texture-sharing] Animation reset" << std::endl;
    }

    // =========================================================================
    // Animated Parameters
    // =========================================================================

    auto& hue = chain.get<HueShift>("hue");
    hue.amount = static_cast<float>(std::fmod(ctx.time() * 0.1, 1.0));
}

VIVID_CHAIN(setup, update)
