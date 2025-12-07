// chain.cpp - Vivid Project Template
//
// This file is hot-reloaded when you save. Edit while running!
//
// Run with: ./build/bin/vivid examples/template
//
// Controls:
//   Tab - Toggle chain visualizer (see your nodes as a graph)
//   F   - Toggle fullscreen
//   Esc - Quit
//
// Structure:
//   setup()  - Called once when chain loads, and again on each hot-reload
//   update() - Called every frame (typically 60fps)
//
// Tips:
//   - Operators connect via .input("name")
//   - Every chain needs an Output operator
//   - Check the terminal for compile errors if hot-reload fails
//   - See docs/LLM-REFERENCE.md for all operators
//   - See docs/RECIPES.md for effect examples

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

// Global chain pointer - must be global for hot-reload to work
Chain* chain = nullptr;

void setup(Context& ctx) {
    // Clean up previous chain on hot-reload
    // This is required to prevent memory leaks
    delete chain;

    // Create a new chain with output dimensions
    // Common sizes: 1280x720 (720p), 1920x1080 (1080p), 3840x2160 (4K)
    chain = new Chain(ctx, 1280, 720);

    // =========================================
    // Add your operators below
    // =========================================

    // GENERATORS create images from nothing:
    //   Noise, SolidColor, Gradient, Ramp, Shape, LFO, Image

    chain->add<Noise>("noise")
        .scale(4.0f)      // Size of noise pattern (higher = smaller details)
        .speed(0.5f)      // Animation speed
        .type(NoiseType::Simplex)
        .octaves(4);      // Layers of detail (more = richer, slower)

    // EFFECTS transform their input:
    //   Blur, HSV, Brightness, Transform, Mirror, Displace, Edge,
    //   Pixelate, Tile, ChromaticAberration, Bloom, Feedback

    chain->add<HSV>("colorize")
        .input("noise")       // Connect to the noise generator
        .hueShift(0.6f)       // Shift hue (0-1 wraps around color wheel)
        .saturation(0.8f)     // Color intensity (0 = grayscale)
        .value(1.0f);         // Brightness multiplier

    // OUTPUT sends to screen - every chain needs exactly one
    chain->add<Output>("out")
        .input("colorize");

    // =========================================
    // Register operators for the visualizer (Tab key)
    // This is optional but helpful for debugging
    // =========================================

    ctx.registerOperator("noise", &chain->get<Noise>("noise"));
    ctx.registerOperator("colorize", &chain->get<HSV>("colorize"));
    ctx.registerOperator("out", &chain->get<Output>("out"));
}

void update(Context& ctx) {
    // Process the chain every frame
    chain->process();

    // =========================================
    // Dynamic updates go here
    // =========================================

    // You can animate parameters using ctx.time():
    // chain->get<Noise>("noise").scale(4.0f + sin(ctx.time()) * 2.0f);

    // Available context values:
    //   ctx.time()   - Seconds since start (float)
    //   ctx.dt()     - Delta time since last frame (float)
    //   ctx.frame()  - Frame number (int)
    //   ctx.width()  - Output width (int)
    //   ctx.height() - Output height (int)
}

// This macro exports setup and update for the vivid runtime
VIVID_CHAIN(setup, update)
