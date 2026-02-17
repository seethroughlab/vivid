// Lesson 04: Images and Assets
// Load and process image files
//
// Run: ./build/bin/vivid projects/getting-started/04-images-assets
//
// Try adding your own images to the assets/ folder!

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Load an image from the assets folder
    auto& img = chain.add<Image>("img");
    img.file = "assets/sample.jpg";

    // Apply color adjustments
    auto& hsv = chain.add<HSV>("hsv");
    hsv.input("img");
    hsv.hueShift = 0.0f;      // Try 0.0 - 1.0 to shift colors
    hsv.saturation = 1.2f;    // > 1.0 = more vivid
    hsv.value = 1.0f;         // Brightness

    // Add a soft glow effect
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("hsv");
    bloom.threshold = 0.6f;   // What brightness triggers glow
    bloom.intensity = 0.25f;  // How strong the glow is
    bloom.radius = 10.0f;     // How far it spreads

    // Add vignette (dark corners)
    auto& vignette = chain.add<Vignette>("vignette");
    vignette.input("bloom");
    vignette.intensity = 0.75f;  // Size of the clear area
    vignette.softness = 0.5f; // Fade smoothness

    // EXPERIMENT: Try adding more effects
    // auto& pixel = chain.add<Pixelate>("pixel");
    // pixel.input("vignette");
    // pixel.size = 4.0f;
    // chain.output("pixel");

    chain.output("vignette");
}

void update(Context& ctx) {
    // EXPERIMENT: Animate the hue shift
    // auto& hsv = ctx.chain().get<HSV>("hsv");
    // hsv.hueShift = fmod(ctx.time() * 0.1f, 1.0f);
}

VIVID_CHAIN(setup, update)
