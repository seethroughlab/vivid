// Telegram Test Project
// Audio-reactive visuals with synth-generated audio (works headless)
//
// Audio: Oscillator → Delay → AudioOutput
// Analysis: Levels + BandSplit on the oscillator
// Visuals: Noise → Feedback → Bloom → output
// Reactivity: bass → noise scale, volume → bloom intensity, energy → feedback decay

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <vivid/audio_output.h>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================
    // Audio: synth drone (works headless, no mic needed)
    // =========================================

    auto& osc = chain.add<Oscillator>("osc");
    osc.frequency = 55.0f;     // Low A1 drone
    osc.volume = 0.4f;
    osc.waveform(Waveform::Saw);

    auto& delay = chain.add<Delay>("delay");
    delay.input("osc");
    delay.delayTime = 400.0f;  // ms
    delay.feedback = 0.5f;
    delay.mix = 0.4f;

    auto& out = chain.add<AudioOutput>("out");
    out.setInput("delay");
    chain.audioOutput("out");

    // =========================================
    // Audio analysis
    // =========================================

    auto& levels = chain.add<Levels>("levels");
    levels.input("osc");
    levels.smoothing = 0.85f;

    auto& bands = chain.add<BandSplit>("bands");
    bands.input("osc");
    bands.smoothing = 0.8f;

    // =========================================
    // Visuals: noise → feedback → bloom
    // =========================================

    auto& noise = chain.add<Noise>("noise");
    noise.scale = 4.0f;
    noise.speed = 0.5f;
    noise.octaves = 3;

    auto& feedback = chain.add<Feedback>("feedback");
    feedback.input("noise");
    feedback.decay = 0.92f;
    feedback.mix = 0.6f;
    feedback.zoom = 1.002f;
    feedback.rotate = 0.003f;

    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("feedback");
    bloom.threshold = 0.5f;
    bloom.intensity = 0.8f;
    bloom.radius = 12.0f;

    chain.output("bloom");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    // Read audio analysis
    auto& levels = chain.get<Levels>("levels");
    auto& bands = chain.get<BandSplit>("bands");

    float volume = levels.rms();
    float bass = bands.bass();
    float mid = bands.mid();
    float high = bands.high();

    // Drive visuals from audio
    auto& noise = chain.get<Noise>("noise");
    auto& feedback = chain.get<Feedback>("feedback");
    auto& bloom = chain.get<Bloom>("bloom");

    // Bass → noise scale (lower bass = bigger patterns)
    noise.scale = 3.0f + bass * 8.0f;

    // Mids → animation speed
    noise.speed = 0.3f + mid * 1.5f;

    // Volume → bloom intensity
    bloom.intensity = 0.4f + volume * 2.0f;

    // High frequencies → bloom threshold (more highs = more glow)
    bloom.threshold = 0.6f - high * 0.3f;

    // Energy → feedback decay (louder = longer trails)
    feedback.decay = 0.88f + volume * 0.08f;

    // Slow rotation modulated by bass
    feedback.rotate = 0.002f + bass * 0.005f;
}

VIVID_CHAIN(setup, update)
