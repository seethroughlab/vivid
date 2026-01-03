// Window Controls Example - Phase 14: Advanced Window & Input
//
// Demonstrates the window control API:
//   B - Toggle borderless window (no title bar/decorations)
//   C - Toggle cursor visibility
//   T - Toggle always-on-top mode
//   1-9 - Move window to monitor 1-9
//   F - Toggle fullscreen
//
// The current state is displayed in the console.

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <iostream>

using namespace vivid;
using namespace vivid::effects;

void printStatus(Context& ctx) {
    std::cout << "\r"
              << "[B]orderless: " << (ctx.borderless() ? "ON " : "OFF")
              << " | [C]ursor: " << (ctx.cursorVisible() ? "SHOW" : "HIDE")
              << " | [T]op: " << (ctx.alwaysOnTop() ? "ON " : "OFF")
              << " | [F]ullscreen: " << (ctx.fullscreen() ? "ON " : "OFF")
              << " | Monitor: " << (ctx.currentMonitor() + 1) << "/" << ctx.monitorCount()
              << "   " << std::flush;
}

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Simple gradient background
    auto& grad = chain.add<Gradient>("gradient");
    grad.colorA.set(0.1f, 0.1f, 0.2f, 1.0f);
    grad.colorB.set(0.2f, 0.1f, 0.3f, 1.0f);

    // Add some visual feedback
    auto& noise = chain.add<Noise>("noise");
    noise.scale = 3.0f;
    noise.speed = 0.2f;

    auto& comp = chain.add<Composite>("comp");
    comp.inputA("gradient");
    comp.inputB("noise");
    comp.mode(BlendMode::Add);
    comp.opacity = 0.3f;

    chain.output("comp");

    std::cout << "\n========================================" << std::endl;
    std::cout << "Window Controls Demo" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  B - Toggle borderless (no decorations)" << std::endl;
    std::cout << "  C - Toggle cursor visibility" << std::endl;
    std::cout << "  T - Toggle always-on-top" << std::endl;
    std::cout << "  F - Toggle fullscreen" << std::endl;
    std::cout << "  1-9 - Move to monitor 1-9" << std::endl;
    std::cout << "  ESC - Exit" << std::endl;
    std::cout << "========================================\n" << std::endl;

    printStatus(ctx);
}

void update(Context& ctx) {
    bool changed = false;

    // Toggle borderless window
    if (ctx.key(GLFW_KEY_B).pressed) {
        ctx.borderless(!ctx.borderless());
        changed = true;
    }

    // Toggle cursor visibility
    if (ctx.key(GLFW_KEY_C).pressed) {
        ctx.cursorVisible(!ctx.cursorVisible());
        changed = true;
    }

    // Toggle always-on-top
    if (ctx.key(GLFW_KEY_T).pressed) {
        ctx.alwaysOnTop(!ctx.alwaysOnTop());
        changed = true;
    }

    // Toggle fullscreen
    if (ctx.key(GLFW_KEY_F).pressed) {
        ctx.fullscreen(!ctx.fullscreen());
        changed = true;
    }

    // Monitor selection (keys 1-9)
    for (int i = 0; i < 9; ++i) {
        if (ctx.key(GLFW_KEY_1 + i).pressed) {
            if (i < ctx.monitorCount()) {
                ctx.moveToMonitor(i);
                changed = true;
            }
        }
    }

    if (changed) {
        printStatus(ctx);
    }
}

VIVID_CHAIN(setup, update)
