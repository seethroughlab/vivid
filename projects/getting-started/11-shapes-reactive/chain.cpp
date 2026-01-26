// Lesson 11: Shapes Reactive
// Audio-reactive visuals without procedural noise
//
// Run: ./build/bin/vivid projects/getting-started/11-shapes-reactive
//
// This lesson demonstrates alternatives to noise for audio-reactive visuals.
// Uses geometric shapes and gradients instead.

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================
    // Audio Input & Analysis
    // =========================================

    auto& audio = chain.add<AudioIn>("audio");
    audio.volume = 1.0f;

    auto& bands = chain.add<BandSplit>("bands");
    bands.input("audio");
    bands.smoothing = 0.85f;

    // =========================================
    // Visuals - No Noise!
    // =========================================

    // Background gradient - radial dark center
    auto& bg = chain.add<Gradient>("bg");
    bg.mode = GradientMode::Radial;
    bg.colorA.set(0.05f, 0.05f, 0.15f, 1.0f);  // Dark center
    bg.colorB.set(0.0f, 0.0f, 0.0f, 1.0f);     // Black edge
    bg.scale = 1.5f;

    // Central star shape - pulses with bass
    auto& star = chain.add<Shape>("star");
    star.type = ShapeType::Star;
    star.sides = 5;
    star.size.set(0.3f, 0.3f);
    star.softness = 0.15f;
    star.position.set(0.5f, 0.5f);
    star.color.set(1.0f, 0.5f, 0.2f, 1.0f);  // Orange

    // Outer ring - responds to mids
    auto& ring = chain.add<Shape>("ring");
    ring.type = ShapeType::Ellipse;
    ring.size.set(0.5f, 0.5f);
    ring.thickness = 0.02f;  // Ring, not filled
    ring.softness = 0.3f;
    ring.position.set(0.5f, 0.5f);
    ring.color.set(0.2f, 0.6f, 1.0f, 1.0f);  // Cyan

    // Composite shapes over background
    auto& comp1 = chain.add<Composite>("comp1");
    comp1.inputA("bg");
    comp1.inputB("ring");
    comp1.mode = BlendMode::Add;

    auto& comp2 = chain.add<Composite>("comp2");
    comp2.inputA("comp1");
    comp2.inputB("star");
    comp2.mode = BlendMode::Add;

    // Colorize based on audio
    auto& hsv = chain.add<HSV>("color");
    hsv.input("comp2");

    // Bloom for glow
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("color");
    bloom.threshold = 0.3f;
    bloom.intensity = 1.0f;
    bloom.radius = 15.0f;

    chain.output("bloom");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    // Get audio analysis
    auto& bands = chain.get<BandSplit>("bands");
    float bass = bands.bass();
    float mid = bands.mid();
    float high = bands.high();

    // =========================================
    // Audio-Driven Animation
    // =========================================

    auto& bg = chain.get<Gradient>("bg");
    auto& star = chain.get<Shape>("star");
    auto& ring = chain.get<Shape>("ring");
    auto& hsv = chain.get<HSV>("color");
    auto& bloom = chain.get<Bloom>("bloom");

    // Bass drives star size (pulsing from center)
    float starSize = 0.15f + bass * 0.35f;
    star.size.set(starSize, starSize);

    // Bass also drives background scale (inverted - smaller = brighter center)
    bg.scale = 2.0f - bass * 0.8f;

    // Mids drive ring size (expanding outward)
    float ringSize = 0.4f + mid * 0.3f;
    ring.size.set(ringSize, ringSize);
    ring.thickness = 0.02f + mid * 0.03f;

    // Time-based rotation
    float t = static_cast<float>(ctx.time());
    star.rotation = t * 0.5f;
    ring.rotation = -t * 0.3f;

    // Audio-driven colors
    star.color.set(
        0.8f + bass * 0.2f,   // Red from bass
        0.4f + mid * 0.3f,    // Green from mid
        0.2f + high * 0.3f,   // Blue from high
        1.0f
    );

    ring.color.set(
        0.2f + high * 0.3f,   // Red from high
        0.5f + mid * 0.3f,    // Green from mid
        0.8f + bass * 0.2f,   // Blue from bass
        1.0f
    );

    // Hue shift from highs
    hsv.hueShift = high * 0.2f;

    // Bloom intensity from overall energy
    bloom.intensity = 0.8f + (bass + mid + high) * 0.5f;
}

VIVID_CHAIN(setup, update)
