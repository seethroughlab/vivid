// Audio Synthesis Test
// Tests: Oscillator, Envelope, AudioFilter, AudioGain, AudioMixer, AudioOutput

#include <vivid/vivid.h>
#include <vivid/audio/oscillator.h>
#include <vivid/audio/envelope.h>
#include <vivid/audio/audio_filter.h>
#include <vivid/audio/audio_gain.h>
#include <vivid/audio/audio_mixer.h>
#include <vivid/audio/audio_output.h>
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
    chain.add<Oscillator>("osc1")
        .frequency(261.63f)  // C4
        .waveform(Oscillator::Waveform::Sawtooth)
        .amplitude(0.3f);

    // Oscillator 2: Square wave slightly detuned
    chain.add<Oscillator>("osc2")
        .frequency(262.5f)   // Slightly detuned
        .waveform(Oscillator::Waveform::Square)
        .amplitude(0.2f);

    // Oscillator 3: Sub bass (sine one octave down)
    chain.add<Oscillator>("sub")
        .frequency(130.81f)  // C3
        .waveform(Oscillator::Waveform::Sine)
        .amplitude(0.25f);

    // ADSR envelope for amplitude
    chain.add<Envelope>("env")
        .attack(0.01f)
        .decay(0.1f)
        .sustain(0.5f)
        .release(0.3f);

    // Mix oscillators
    chain.add<AudioMixer>("osc_mix")
        .input(0, "osc1")
        .input(1, "osc2")
        .input(2, "sub");

    // Apply envelope to mixed signal
    chain.add<AudioGain>("enveloped")
        .input("osc_mix")
        .gainInput("env");

    // Low-pass filter for warmth
    chain.add<AudioFilter>("filter")
        .input("enveloped")
        .type(AudioFilter::Type::LowPass)
        .cutoff(2000.0f)
        .resonance(0.3f);

    // Final gain
    chain.add<AudioGain>("master")
        .input("filter")
        .gain(0.5f);

    // Audio output
    chain.add<AudioOutput>("audioOut")
        .input("master")
        .volume(0.8f);

    // Audio analysis for visualization
    chain.add<Levels>("levels")
        .input("master");

    // Visual representation
    auto& bg_noise = chain.add<Noise>("bg_noise");
    bg_noise.set("scale", 4.0f).set("speed", 0.1f);

    auto& bg_color = chain.add<HSV>("bg_color");
    bg_color.input(&bg_noise);
    bg_color.hue(0.6f).saturation(0.3f).value(0.2f);

    // Pulsing circle based on audio levels
    chain.add<Shape>("pulse")
        .type(Shape::Type::Circle)
        .sizeInput("levels")
        .sizeScale(0.4f)
        .color(0.3f, 0.8f, 1.0f, 0.8f);

    chain.add<Composite>("visual")
        .input(0, "bg_color")
        .input(1, "pulse")
        .mode(Composite::Mode::Add);

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
        if (auto* osc1 = chain.get<Oscillator>("osc1")) {
            osc1->frequency(freq);
        }
        if (auto* osc2 = chain.get<Oscillator>("osc2")) {
            osc2->frequency(freq * 1.003f);  // Slight detune
        }
        if (auto* sub = chain.get<Oscillator>("sub")) {
            sub->frequency(freq * 0.5f);  // One octave down
        }

        // Trigger envelope
        if (auto* env = chain.get<Envelope>("env")) {
            env->trigger();
        }
    }
}

VIVID_CHAIN(setup, update)
