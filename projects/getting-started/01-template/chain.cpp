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
//   - Operators connect via operator.input("other")
//   - Use chain.output("name") to specify what displays
//   - Check the terminal for compile errors if hot-reload fails
//   - See docs/RECIPES.md for effect examples
//   - See modules/vivid-core/examples/ for working examples

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

    // Create an animated color gradient
    auto& gradient = chain.add<Gradient>("gradient");
    gradient.mode = GradientMode::Linear;
    gradient.angle = 0.785f;  // 45 degrees
    gradient.colorA.set(0.2f, 0.4f, 0.9f, 1.0f);  // Blue
    gradient.colorB.set(0.9f, 0.3f, 0.5f, 1.0f);  // Pink

    // Create animated noise for displacement
    auto& noise = chain.add<Noise>("noise");
    noise.type = NoiseType::Simplex;
    noise.scale = 3.0f;      // Size of noise pattern (higher = smaller details)
    noise.speed = 0.3f;      // Animation speed
    noise.octaves = 3;       // Layers of detail (more = richer, slower)

    // EFFECTS transform their input:
    //   Blur, HSV, Brightness, Transform, Mirror, Displace, Edge,
    //   Pixelate, Tile, ChromaticAberration, Bloom, Feedback

    // Use noise to displace the gradient (creates flowing, organic motion)
    auto& displace = chain.add<Displace>("displace");
    displace.source("gradient");      // What to displace
    displace.map("noise");            // What drives the displacement
    displace.strength = 0.15f;        // How much to displace (0-1)

    // Specify output - this is what gets displayed
    chain.output("displace");
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
