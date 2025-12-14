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
//   - Operators connect via .input(&operator)
//   - Use chain.output("name") to specify what displays
//   - Check the terminal for compile errors if hot-reload fails
//   - See docs/LLM-REFERENCE.md for all operators
//   - See docs/RECIPES.md for effect examples

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================
    // Add your operators below
    // =========================================

    // GENERATORS create images from nothing:
    //   Noise, SolidColor, Gradient, Ramp, Shape, LFO, Image

    auto& noise = chain.add<Noise>("noise");
    noise.type(NoiseType::Simplex);
    noise.scale = 4.0f;      // Size of noise pattern (higher = smaller details)
    noise.speed = 0.5f;      // Animation speed
    noise.octaves = 4;       // Layers of detail (more = richer, slower)

    // EFFECTS transform their input:
    //   Blur, HSV, Brightness, Transform, Mirror, Displace, Edge,
    //   Pixelate, Tile, ChromaticAberration, Bloom, Feedback

    auto& colorize = chain.add<HSV>("colorize")
        .input(&noise)        // Connect to the noise generator
        .hueShift(0.6f)       // Shift hue (0-1 wraps around color wheel)
        .saturation(0.8f)     // Color intensity (0 = grayscale)
        .value(1.0f);         // Brightness multiplier

    // Specify output - this is what gets displayed
    chain.output("colorize");
}

void update(Context& ctx) {
    // =========================================
    // Dynamic updates go here
    // =========================================

    // Toggle fullscreen with F key
    if (ctx.key(GLFW_KEY_F).pressed) {
        ctx.fullscreen(!ctx.fullscreen());
    }

    // You can animate parameters using ctx.time():
    // ctx.chain().get<Noise>("noise").set("scale", 4.0f + sin(ctx.time()) * 2.0f);

    // Available context values:
    //   ctx.time()   - Seconds since start (float)
    //   ctx.dt()     - Delta time since last frame (float)
    //   ctx.frame()  - Frame number (int)
    //   ctx.width()  - Output width (int)
    //   ctx.height() - Output height (int)
}

// This macro exports setup and update for the vivid runtime
VIVID_CHAIN(setup, update)
