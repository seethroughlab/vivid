// Chain API Demo
// Demonstrates the declarative Chain API for composing operators
//
// This example creates an animated visual using:
// - Noise generator as the base pattern
// - Feedback for trails effect
// - Mirror for kaleidoscope symmetry
// - HSV for color cycling
//
// Controls:
//   Mouse X: Rotation speed
//   Mouse Y: Zoom amount

#include <vivid/vivid.h>

using namespace vivid;

// Setup is called once when the chain is first loaded
void setup(Chain& chain) {
    // Create animated noise as the base pattern
    chain.add<Noise>("noise")
        .scale(4.0f)
        .speed(0.3f)
        .octaves(4);  // Fractal noise

    // Feedback creates trails/tunnel effect
    chain.add<Feedback>("feedback")
        .input("noise")
        .decay(0.92f)
        .zoom(1.02f)
        .rotate(0.01f);

    // Mirror adds kaleidoscope symmetry
    chain.add<Mirror>("mirror")
        .input("feedback")
        .kaleidoscope(6);

    // HSV for color cycling (colorize mode for grayscale input)
    chain.add<HSV>("color")
        .input("mirror")
        .colorize(true)
        .saturation(0.8f)
        .brightness(1.05f);

    // Set the output
    chain.setOutput("color");
}

// Update is called every frame - use for dynamic parameter changes
void update(Chain& chain, Context& ctx) {
    // Mouse X controls rotation speed
    float rotation = (ctx.mouseNormX() - 0.5f) * 0.1f;
    chain.get<Feedback>("feedback").rotate(rotation);

    // Mouse Y controls zoom
    float zoom = 0.98f + ctx.mouseNormY() * 0.06f;
    chain.get<Feedback>("feedback").zoom(zoom);

    // Cycle hue over time
    float hue = std::fmod(ctx.time() * 0.05f, 1.0f);
    chain.get<HSV>("color").hueShift(hue);

    // Clear feedback on click
    if (ctx.wasMousePressed(0)) {
        chain.get<Feedback>("feedback").decay(0.0f);
    } else {
        chain.get<Feedback>("feedback").decay(0.92f);
    }
}

// Export the entry points
VIVID_CHAIN(setup, update)
