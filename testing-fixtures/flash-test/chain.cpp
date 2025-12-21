// Flash Test - Manual trigger demo
//
// Tests the Flash operator with keyboard triggers
// Press 1/2/3 to trigger flashes with different colors

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <iostream>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Visual: animated noise background
    auto& noise = chain.add<Noise>("noise");
    noise.scale = 3.0f;
    noise.octaves = 3;
    noise.speed = 0.5f;

    // Kick flash - white, fast decay (additive)
    auto& kickFlash = chain.add<Flash>("kickFlash");
    kickFlash.input(&noise);
    kickFlash.decay = 0.85f;
    kickFlash.color.set(1.0f, 1.0f, 1.0f);
    kickFlash.mode = 0;  // Additive

    // Snare flash - orange, slower decay (screen)
    auto& snareFlash = chain.add<Flash>("snareFlash");
    snareFlash.input(&kickFlash);
    snareFlash.decay = 0.92f;
    snareFlash.color.set(1.0f, 0.6f, 0.2f);
    snareFlash.mode = 1;  // Screen

    // Hat flash - cyan, medium decay (replace)
    auto& hatFlash = chain.add<Flash>("hatFlash");
    hatFlash.input(&snareFlash);
    hatFlash.decay = 0.88f;
    hatFlash.color.set(0.2f, 0.8f, 1.0f);
    hatFlash.mode = 2;  // Replace

    chain.output("hatFlash");

    std::cout << "\n";
    std::cout << "Flash Test\n";
    std::cout << "==========\n";
    std::cout << "Press 1: White flash (additive)\n";
    std::cout << "Press 2: Orange flash (screen)\n";
    std::cout << "Press 3: Cyan flash (replace)\n";
    std::cout << "Press SPACE: All flashes\n";
    std::cout << "\n";
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    auto& kickFlash = chain.get<Flash>("kickFlash");
    auto& snareFlash = chain.get<Flash>("snareFlash");
    auto& hatFlash = chain.get<Flash>("hatFlash");

    // Manual triggers
    if (ctx.key(GLFW_KEY_1).pressed) {
        kickFlash.trigger();
    }
    if (ctx.key(GLFW_KEY_2).pressed) {
        snareFlash.trigger();
    }
    if (ctx.key(GLFW_KEY_3).pressed) {
        hatFlash.trigger();
    }
    if (ctx.key(GLFW_KEY_SPACE).pressed) {
        kickFlash.trigger();
        snareFlash.trigger();
        hatFlash.trigger();
    }

    // Auto-trigger for demo (every 0.5 seconds)
    static float autoTimer = 0.0f;
    autoTimer += ctx.dt();
    if (autoTimer > 0.5f) {
        autoTimer = 0.0f;
        kickFlash.trigger(0.5f);  // Half intensity
    }

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
