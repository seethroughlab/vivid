// Lesson 05: Audio-Reactive
// Make visuals respond to sound
//
// Run: ./build/bin/vivid projects/getting-started/05-audio-reactive
//
// Play some music or make sounds - watch the visuals react!

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

    // Capture audio from microphone
    auto& audio = chain.add<AudioIn>("audio");
    audio.volume = 1.0f;

    // Analyze overall volume
    auto& levels = chain.add<Levels>("levels");
    levels.input("audio");
    levels.smoothing = 0.85f;  // Smooth out the response

    // Separate into frequency bands
    auto& bands = chain.add<BandSplit>("bands");
    bands.input("audio");
    bands.smoothing = 0.8f;

    // =========================================
    // Visuals
    // =========================================

    // Background noise - scale reacts to bass
    auto& noise = chain.add<Noise>("noise");
    noise.scale = 6.0f;
    noise.speed = 0.5f;
    noise.octaves = 4;

    // Color the noise
    auto& colorize = chain.add<Colorize>("colorize");
    colorize.input("noise");
    colorize.colorA.set(0.1f, 0.1f, 0.3f, 1.0f);  // Dark blue
    colorize.colorB.set(1.0f, 0.4f, 0.1f, 1.0f);  // Orange

    // Add glow - intensity reacts to highs
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("colorize");
    bloom.threshold = 0.4f;
    bloom.intensity = 0.3f;

    chain.output("bloom");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    // Get the analysis operators
    auto& levels = chain.get<Levels>("levels");
    auto& bands = chain.get<BandSplit>("bands");

    // Get audio values (0.0 to 1.0)
    float volume = levels.rms();
    float bass = bands.bass();
    float mid = bands.mid();
    float high = bands.high();

    // =========================================
    // Drive visuals from audio
    // =========================================

    auto& noise = chain.get<Noise>("noise");
    auto& colorize = chain.get<Colorize>("colorize");
    auto& bloom = chain.get<Bloom>("bloom");

    // Bass controls noise scale (bigger = more zoomed in)
    noise.scale = 4.0f + bass * 15.0f;

    // Mids control animation speed
    noise.speed = 0.3f + mid * 2.0f;

    // Volume controls color intensity
    colorize.colorB.set(
        0.8f + volume * 0.2f,   // Red increases with volume
        0.3f + bass * 0.3f,    // Green increases with bass
        0.1f + high * 0.4f,    // Blue increases with highs
        1.0f
    );

    // Highs control bloom intensity
    bloom.intensity = 0.2f + high * 0.6f;

    // Fullscreen toggle
    if (ctx.key(GLFW_KEY_F).pressed) {
        ctx.fullscreen(!ctx.fullscreen());
    }
}

VIVID_CHAIN(setup, update)
