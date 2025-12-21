// WavetableSynth Test Fixture
//
// Demonstrates wavetable morphing through different built-in tables.
// Audio: WavetableSynth with LFO-modulated position playing arpeggios.
// Visual: Simple audio-reactive noise.

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <vivid/audio_output.h>
#include <iostream>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

// Arpeggio note frequencies (C major scale)
static const float NOTES[] = {
    freq::C4, freq::E4, freq::G4, freq::C5,
    freq::G4, freq::E4
};
static const int NUM_NOTES = 6;

// Store chain pointer for sequencer callback
static Chain* g_chain = nullptr;
static int g_noteIndex = 0;

void setup(Context& ctx) {
    auto& chain = ctx.chain();
    g_chain = &chain;

    std::cout << "\n";
    std::cout << "========================================\n";
    std::cout << "  WAVETABLE SYNTH TEST\n";
    std::cout << "========================================\n";
    std::cout << "Demonstrating wavetable morphing\n";
    std::cout << "Position modulated by LFO for evolving timbre\n";
    std::cout << "\n";
    std::cout << "Press ESC to exit\n";
    std::cout << "========================================\n\n";

    // =========================================================================
    // AUDIO: WavetableSynth with Clock + Sequencer
    // =========================================================================

    // Clock at 120 BPM
    auto& clock = chain.add<Clock>("clock");
    clock.bpm = 120.0f;
    clock.division(ClockDiv::Sixteenth);

    // Sequencer for arpeggios
    auto& seq = chain.add<Sequencer>("seq");
    seq.steps = 16;
    seq.setPattern(0b1010101010101010);  // Every other step

    // WavetableSynth - main sound source
    auto& wt = chain.add<WavetableSynth>("wt");
    wt.loadBuiltin(BuiltinTable::Analog);
    wt.maxVoices = 4;
    wt.volume = 0.6f;
    wt.attack = 0.02f;
    wt.decay = 0.1f;
    wt.sustain = 0.5f;
    wt.release = 0.3f;
    wt.detune = 5.0f;  // Slight stereo spread

    // LFO to modulate wavetable position (visual effects LFO)
    auto& posLFO = chain.add<LFO>("posLFO");
    posLFO.frequency = 0.1f;  // Slow sweep through wavetable
    posLFO.amplitude = 0.5f;
    posLFO.offset = 0.5f;
    posLFO.waveform(LFOWaveform::Triangle);

    // Reverb for space
    auto& reverb = chain.add<Reverb>("reverb");
    reverb.roomSize = 0.7f;
    reverb.damping = 0.4f;
    reverb.mix = 0.3f;
    reverb.input("wt");

    // Delay for rhythmic interest
    auto& delay = chain.add<Delay>("delay");
    delay.delayTime = 375.0f;  // Dotted eighth at 120 BPM
    delay.feedback = 0.4f;
    delay.mix = 0.25f;
    delay.input("reverb");

    // Output
    auto& audioOut = chain.add<AudioOutput>("audioOut");
    audioOut.setInput("delay");
    chain.audioOutput("audioOut");

    // Analysis for visuals
    auto& levels = chain.add<Levels>("levels");
    levels.input("wt");

    // =========================================================================
    // VISUALS: Simple audio-reactive noise
    // =========================================================================

    // Noise layer
    auto& noise = chain.add<Noise>("noise");
    noise.type(NoiseType::Simplex);
    noise.scale = 4.0f;
    noise.octaves = 3;
    noise.speed = 0.2f;

    // Flash on triggers
    auto& flash = chain.add<Flash>("flash");
    flash.input("noise");
    flash.decay = 0.9f;
    flash.color.set(0.2f, 0.8f, 0.6f);

    chain.output("flash");

    // =========================================================================
    // Trigger callback for arpeggio with visual flash
    // =========================================================================

    auto* chainPtr = &chain;
    seq.onTrigger([chainPtr](float vel) {
        auto& wt = chainPtr->get<WavetableSynth>("wt");
        auto& flash = chainPtr->get<Flash>("flash");

        // Play next note in arpeggio
        float freq = NOTES[g_noteIndex];
        wt.noteOn(freq);
        g_noteIndex = (g_noteIndex + 1) % NUM_NOTES;

        // Visual feedback
        flash.trigger(vel);
    });
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    // Advance clock and check for triggers
    auto& clock = chain.get<Clock>("clock");
    if (clock.triggered()) {
        chain.get<Sequencer>("seq").advance();
    }

    // Modulate wavetable position with LFO
    float posLFO = chain.get<LFO>("posLFO").outputValue();
    chain.get<WavetableSynth>("wt").position = posLFO;

    // Visual modulation from audio
    float level = chain.get<Levels>("levels").peak();

    // Noise reacts to audio level
    chain.get<Noise>("noise").scale = 3.0f + level * 5.0f;

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
