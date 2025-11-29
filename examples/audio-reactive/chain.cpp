/**
 * Audio-Reactive Example
 *
 * Demonstrates audio input with frequency analysis driving visual effects.
 *
 * Features:
 * - Microphone/line-in capture
 * - FFT frequency analysis
 * - Bass, mid, and treble band separation
 * - Visuals that respond to audio levels
 */

#include <vivid/vivid.h>
#include <cmath>

using namespace vivid;

void setup(Chain& chain) {
    // Audio input with FFT analysis
    chain.add<AudioIn>("audio")
        .device(-1)        // Default input device
        .gain(2.0f)        // Boost input gain
        .fftSize(1024)     // FFT window size
        .smoothing(0.85f); // Smooth band values

    // Base noise pattern - scale affected by bass
    chain.add<Noise>("noise")
        .scale(3.0f)
        .speed(0.2f)
        .octaves(3);

    // Central shape that pulses with overall level
    chain.add<Shape>("centerShape")
        .type(Shape::Circle)
        .center(glm::vec2(0.5f, 0.5f))
        .radius(0.15f)
        .color(glm::vec3(1.0f, 0.3f, 0.5f))
        .softness(0.02f);

    // Ring that expands with bass
    chain.add<Shape>("bassRing")
        .type(Shape::Ring)
        .center(glm::vec2(0.5f, 0.5f))
        .radius(0.25f)
        .innerRadius(0.20f)
        .color(glm::vec3(0.2f, 0.5f, 1.0f))
        .softness(0.01f);

    // Composite the shapes over noise
    chain.add<Composite>("scene")
        .inputs({"noise", "bassRing", "centerShape"})
        .mode(0)  // Over
        .opacity(1.0f);

    // Feedback creates trails - decay affected by mid frequencies
    chain.add<Feedback>("fb")
        .input("scene")
        .decay(0.9f)
        .zoom(1.01f);

    // Mirror for kaleidoscope effect
    chain.add<Mirror>("mirror")
        .input("fb")
        .kaleidoscope(6);

    // HSV color shift - hue driven by treble
    chain.add<HSV>("color")
        .input("mirror")
        .colorize(true)
        .saturation(0.8f);

    // Bloom for glow effect - intensity from level
    chain.add<Bloom>("bloom")
        .input("color")
        .threshold(0.5f)
        .intensity(0.8f)
        .radius(10.0f);

    chain.setOutput("bloom");
}

void update(Chain& chain, Context& ctx) {
    // Get audio analysis values from the AudioIn operator
    // These are set each frame by the AudioIn::process() method
    float level = ctx.getInputValue("audio", "level");
    float bass = ctx.getInputValue("audio", "bass");
    float mid = ctx.getInputValue("audio", "mid");
    float high = ctx.getInputValue("audio", "high");

    // Scale values for visual impact (audio values are typically 0-0.3)
    float levelScaled = level * 5.0f;
    float bassScaled = bass * 8.0f;
    float midScaled = mid * 6.0f;
    float highScaled = high * 10.0f;

    // Noise scale responds to bass (deeper = larger patterns)
    float noiseScale = 3.0f + bassScaled * 2.0f;
    chain.get<Noise>("noise").scale(noiseScale);

    // Center shape pulses with overall level
    float centerRadius = 0.1f + levelScaled * 0.1f;
    chain.get<Shape>("centerShape").radius(centerRadius);

    // Bass ring expands with bass
    float ringRadius = 0.2f + bassScaled * 0.15f;
    chain.get<Shape>("bassRing")
        .radius(ringRadius + 0.05f)
        .innerRadius(ringRadius);

    // Feedback rotation from mid frequencies
    float rotation = (midScaled - 0.5f) * 0.05f;
    chain.get<Feedback>("fb")
        .rotate(rotation)
        .decay(0.85f + midScaled * 0.1f);

    // Hue shift from treble
    float hueShift = ctx.time() * 0.05f + highScaled * 0.3f;
    chain.get<HSV>("color").hueShift(hueShift);

    // Bloom intensity from level
    float bloomIntensity = 0.5f + levelScaled * 0.8f;
    chain.get<Bloom>("bloom").intensity(bloomIntensity);
}

VIVID_CHAIN(setup, update)
