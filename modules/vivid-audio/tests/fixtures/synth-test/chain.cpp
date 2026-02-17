// Audio Synthesis Test
// Tests: Oscillator, Envelope, AudioFilter, AudioGain, AudioMixer, AudioOutput

#include <vivid/vivid.h>
#include <vivid/audio/oscillator.h>
#include <vivid/audio/envelope.h>
#include <vivid/audio/audio_filter.h>
#include <vivid/audio/audio_gain.h>
#include <vivid/audio/audio_mixer.h>
#include <vivid/audio_output.h>
#include <vivid/audio/levels.h>
#include <vivid/effects/noise.h>
#include <vivid/effects/hsv.h>
#include <vivid/effects/shape.h>
#include <vivid/effects/composite.h>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

static float notePhase = 0.0f;
static float noteTime = 0.0f;
static int noteIndex = 0;

// Simple melody: C4, E4, G4, C5 (arpeggio)
static const float melody[] = {261.63f, 329.63f, 392.0f, 523.25f};
static const int melodyLength = 4;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Oscillator 1: Saw wave for rich harmonics
    auto& osc1 = chain.add<Oscillator>("osc1");
    osc1.frequency = 261.63f;  // C4
    osc1.waveform(Waveform::Saw);
    osc1.volume = 0.3f;

    // Oscillator 2: Square wave slightly detuned
    auto& osc2 = chain.add<Oscillator>("osc2");
    osc2.frequency = 262.5f;   // Slightly detuned
    osc2.waveform(Waveform::Square);
    osc2.volume = 0.2f;

    // Oscillator 3: Sub bass (sine one octave down)
    auto& sub = chain.add<Oscillator>("sub");
    sub.frequency = 130.81f;  // C3
    sub.waveform(Waveform::Sine);
    sub.volume = 0.25f;

    // ADSR envelope for amplitude
    auto& env = chain.add<Envelope>("env");
    env.attack = 0.01f;
    env.decay = 0.1f;
    env.sustain = 0.5f;
    env.release = 0.3f;

    // Mix oscillators
    auto& osc_mix = chain.add<AudioMixer>("osc_mix");
    osc_mix.input(0, "osc1");
    osc_mix.input(1, "osc2");
    osc_mix.input(2, "sub");

    // Apply envelope to mixed signal
    auto& enveloped = chain.add<AudioGain>("enveloped");
    enveloped.input("osc_mix");
    enveloped.setGainInput("env");

    // Low-pass filter for warmth
    auto& filter = chain.add<AudioFilter>("filter");
    filter.setInputByName(0, "enveloped");
    filter.setType(FilterType::Lowpass);
    filter.cutoff = 2000.0f;
    filter.resonance = 0.3f;

    // Final gain
    auto& master = chain.add<AudioGain>("master");
    master.input("filter");
    master.gain = 0.5f;

    // Audio output
    auto& audioOut = chain.add<AudioOutput>("audioOut");
    audioOut.input("master");
    audioOut.setVolume(0.8f);

    // Audio analysis for visualization
    auto& levels = chain.add<Levels>("levels");
    levels.input("master");

    // Visual representation
    auto& bg_noise = chain.add<Noise>("bg_noise");
    bg_noise.set("scale", 4.0f);
    bg_noise.set("speed", 0.1f);

    auto& bg_color = chain.add<HSV>("bg_color");
    bg_color.input("bg_noise");
    bg_color.hueShift = 0.6f;
    bg_color.saturation = 0.3f;
    bg_color.value = 0.2f;

    // Pulsing circle based on audio levels
    auto& pulse = chain.add<Shape>("pulse");
    pulse.type = ShapeType::Ellipse;
    pulse.size.set(0.4f, 0.4f);
    pulse.color.set(0.3f, 0.8f, 1.0f, 0.8f);

    auto& visual = chain.add<Composite>("visual");
    visual.input(0, "bg_color");
    visual.input(1, "pulse");
    visual.mode = BlendMode::Add;

    chain.output("visual");
    chain.audioOutput("audioOut");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float dt = ctx.dt();

    // Simple note sequencer
    noteTime += dt;
    if (noteTime > 0.25f) {  // Change note every 250ms
        noteTime = 0.0f;
        noteIndex = (noteIndex + 1) % melodyLength;
        float freq = melody[noteIndex];

        // Update oscillator frequencies
        auto& osc1 = chain.get<Oscillator>("osc1");
        osc1.frequency = freq;

        auto& osc2 = chain.get<Oscillator>("osc2");
        osc2.frequency = freq * 1.003f;  // Slight detune

        auto& sub = chain.get<Oscillator>("sub");
        sub.frequency = freq * 0.5f;  // One octave down

        // Trigger envelope
        auto& env = chain.get<Envelope>("env");
        env.trigger();
    }
}

VIVID_CHAIN(setup, update)
