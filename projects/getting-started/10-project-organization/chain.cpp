// Lesson 10: Project Organization
// A well-structured example project demonstrating best practices
//
// Run: ./build/bin/vivid projects/getting-started/10-project-organization
//
// This project combines concepts from all previous lessons into
// a complete, well-organized example.

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

// ============================================================================
// SECTION 1: AUDIO CAPTURE & ANALYSIS
// ============================================================================

void setupAudio(Chain& chain) {
    auto& audio = chain.add<AudioIn>("audio");
    audio.volume = 1.0f;

    auto& levels = chain.add<Levels>("levels");
    levels.input("audio");
    levels.smoothing = 0.85f;

    auto& bands = chain.add<BandSplit>("bands");
    bands.input("audio");
    bands.smoothing = 0.8f;
}

// ============================================================================
// SECTION 2: VISUAL GENERATION
// ============================================================================

void setupVisuals(Chain& chain) {
    // Base layer: animated noise
    auto& noise = chain.add<Noise>("noise");
    noise.scale = 6.0f;
    noise.speed = 0.3f;
    noise.octaves = 4;

    // Color layer: gradient that shifts with time
    auto& colorGrad = chain.add<Gradient>("colorGrad");
    colorGrad.colorA.set(0.1f, 0.1f, 0.3f, 1.0f);
    colorGrad.colorB.set(0.9f, 0.4f, 0.2f, 1.0f);

    auto& colorize = chain.add<Lookup>("colorize");
    colorize.input("noise");
    colorize.lut("colorGrad");

    // Shape layer: pulsing circle
    auto& shape = chain.add<Shape>("shape");
    shape.type = ShapeType::Ellipse;
    shape.size.set(0.3f, 0.3f);
    shape.softness = 0.05f;
    shape.color.set(1.0f, 1.0f, 1.0f, 1.0f);
}

// ============================================================================
// SECTION 3: COMPOSITING
// ============================================================================

void setupCompositing(Chain& chain) {
    // Combine noise and shape
    auto& comp = chain.add<Composite>("comp");
    comp.inputA("colorize");
    comp.inputB("shape");
    comp.mode = BlendMode::Add;
    comp.opacity = 0.8f;

    // Post-processing: bloom for glow
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("comp");
    bloom.threshold = 0.4f;
    bloom.intensity = 0.3f;
    bloom.radius = 12.0f;

    // Final output
    chain.output("bloom");
}

// ============================================================================
// SETUP: Initialize all systems
// ============================================================================

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    setupAudio(chain);
    setupVisuals(chain);
    setupCompositing(chain);
}

// ============================================================================
// UPDATE: Per-frame logic
// ============================================================================

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = static_cast<float>(ctx.time());

    // Get audio analysis
    auto& levels = chain.get<Levels>("levels");
    auto& bands = chain.get<BandSplit>("bands");

    float rms = levels.rms();
    float bass = bands.bass();
    float mid = bands.mid();
    float high = bands.high();

    // ========================================
    // Audio-reactive noise
    // ========================================
    auto& noise = chain.get<Noise>("noise");
    noise.scale = 4.0f + bass * 8.0f;
    noise.speed = 0.2f + mid * 0.8f;

    // ========================================
    // Audio-reactive shape
    // ========================================
    auto& shape = chain.get<Shape>("shape");
    float pulseSize = 0.2f + rms * 0.3f;
    shape.size.set(pulseSize, pulseSize);

    // ========================================
    // Audio-reactive color
    // ========================================
    auto& colorGrad = chain.get<Gradient>("colorGrad");

    // Shift hue over time
    float hueShift = fmod(time * 0.1f, 1.0f);
    float r = 0.5f + 0.5f * sin(hueShift * 6.28f);
    float g = 0.5f + 0.5f * sin((hueShift + 0.33f) * 6.28f);
    float b = 0.5f + 0.5f * sin((hueShift + 0.66f) * 6.28f);
    colorGrad.colorB.set(r, g, b, 1.0f);

    // ========================================
    // Audio-reactive post-processing
    // ========================================
    auto& bloom = chain.get<Bloom>("bloom");
    bloom.intensity = 0.2f + high * 0.5f;
}

// ============================================================================
// CONFIGURATION
// ============================================================================

static vivid::ChainConfig config{
    .windowWidth = 1280,
    .windowHeight = 720,
    .resizable = true
};

VIVID_CHAIN_CONFIG(setup, update, config)
