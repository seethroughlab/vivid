// Hello World Vivid Example
// A simple operator chain that outputs animated noise
//
// This demonstrates the Chain API using built-in operators.
// Parameters can be adjusted via the VS Code extension.

#include <vivid/vivid.h>

using namespace vivid;

// Setup is called once when the chain is first loaded
void setup(Chain& chain) {
    // Create animated noise
    chain.add<Noise>("noise")
        .scale(4.0f)
        .speed(1.0f)
        .octaves(4);

    // Apply HSV color transformation
    chain.add<HSV>("color")
        .input("noise")
        .saturation(1.5f)
        .brightness(1.0f);

    // Set the output
    chain.setOutput("color");
}

// Update is called every frame - use for dynamic parameter changes
void update(Chain& chain, Context& ctx) {
    // Cycle hue over time for animated colors
    float hue = std::fmod(ctx.time() * 0.1f, 1.0f);
    chain.get<HSV>("color").hueShift(hue);

    // === Window Management Keys ===
    // F11 or F: Toggle fullscreen
    if (ctx.wasKeyPressed(Key::F11) || ctx.wasKeyPressed(Key::F)) {
        ctx.toggleFullscreen();
    }
    // B: Toggle borderless
    if (ctx.wasKeyPressed(Key::B)) {
        ctx.setBorderless(!ctx.isBorderless());
    }
    // C: Toggle cursor visibility
    if (ctx.wasKeyPressed(Key::C)) {
        ctx.setCursorVisible(!ctx.isCursorVisible());
    }
    // T: Toggle always-on-top
    if (ctx.wasKeyPressed(Key::T)) {
        ctx.setAlwaysOnTop(!ctx.isAlwaysOnTop());
    }
    // M: Print monitors
    if (ctx.wasKeyPressed(Key::M)) {
        ctx.printMonitors();
    }
    // 1-9: Move to monitor
    for (int i = 0; i < 9; i++) {
        if (ctx.wasKeyPressed(Key::Num1 + i)) {
            ctx.moveToMonitor(i);
        }
    }
}

// Export the entry points
VIVID_CHAIN(setup, update)
