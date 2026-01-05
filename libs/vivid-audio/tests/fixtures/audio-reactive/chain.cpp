// Testing Fixture: Audio Reactive Visuals
// Tests Levels analyzer driving visual parameters
//
// Visual verification:
// - Shape size pulses with audio amplitude
// - Color shifts with audio level
// - Bloom responds to peak detection

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Audio source - simple synth
    auto& synth = chain.add<Synth>("synth");
    synth.setWaveform(Waveform::Saw);
    synth.frequency = 220.0f;
    synth.volume = 0.4f;

    // Audio analysis
    auto& levels = chain.add<Levels>("levels");
    levels.input("synth");
    levels.smoothing = 0.85f;

    // Visual elements
    auto& bg = chain.add<Gradient>("bg");
    bg.mode(GradientMode::Radial);
    bg.colorA.set(0.1f, 0.1f, 0.2f, 1.0f);
    bg.colorB.set(0.05f, 0.02f, 0.1f, 1.0f);

    auto& shape = chain.add<Shape>("shape");
    shape.type(ShapeType::Circle);
    shape.size.set(0.3f, 0.3f);
    shape.color.set(1.0f, 0.5f, 0.2f, 1.0f);
    shape.softness = 0.1f;

    auto& ring = chain.add<Shape>("ring");
    ring.type(ShapeType::Ring);
    ring.size.set(0.5f, 0.5f);
    ring.thickness = 0.02f;
    ring.color.set(1.0f, 1.0f, 1.0f, 0.5f);

    // Compositing
    auto& comp1 = chain.add<Composite>("comp1");
    comp1.inputA("bg");
    comp1.inputB("ring");
    comp1.mode(BlendMode::Add);

    auto& comp2 = chain.add<Composite>("comp2");
    comp2.inputA("comp1");
    comp2.inputB("shape");
    comp2.mode(BlendMode::Add);

    // Bloom
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("comp2");
    bloom.threshold = 0.5f;
    bloom.intensity = 0.8f;

    chain.output("bloom");

    if (chain.hasError()) {
        ctx.setError(chain.error());
    }
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = static_cast<float>(ctx.time());

    // Modulate synth
    auto& synth = chain.get<Synth>("synth");
    synth.frequency = 220.0f + std::sin(t * 0.5f) * 100.0f;
    synth.volume = 0.3f + std::sin(t * 2.0f) * 0.2f;

    // Get audio levels
    auto& levels = chain.get<Levels>("levels");
    float rms = levels.rms();
    float peak = levels.peak();

    // Drive visuals with audio
    auto& shape = chain.get<Shape>("shape");
    shape.size.set(0.2f + rms * 0.3f, 0.2f + rms * 0.3f);
    shape.color.set(1.0f, 0.3f + rms * 0.5f, 0.2f, 1.0f);

    auto& ring = chain.get<Shape>("ring");
    ring.size.set(0.4f + peak * 0.2f, 0.4f + peak * 0.2f);
    ring.color.set(1.0f, 1.0f, 1.0f, 0.3f + peak * 0.7f);

    auto& bloom = chain.get<Bloom>("bloom");
    bloom.intensity = 0.5f + peak * 1.0f;
}

VIVID_CHAIN(setup, update)
