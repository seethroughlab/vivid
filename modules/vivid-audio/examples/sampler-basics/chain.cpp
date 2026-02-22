/**
 * Sampler Basics Example
 *
 * Demonstrates: Sampler, SampleBank, SamplePlayer
 *
 * Two sampler paradigms:
 * 1. Sampler: loads one WAV, plays chromatically with pitch-shifting and ADSR
 * 2. SampleBank + SamplePlayer: loads a folder of WAVs, triggers by index/name
 */

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>

using namespace vivid;
using namespace vivid::audio;
using namespace vivid::effects;

// Notes for a simple arpeggio pattern (MIDI note numbers)
static const uint8_t arpNotes[] = {60, 64, 67, 72, 67, 64};  // C major arp
static int arpIndex = 0;
static int prevNote = -1;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Master clock
    auto& clock = chain.add<Clock>("clock");
    clock.bpm = 120.0f;
    clock.division(ClockDiv::Eighth);

    // =========================================================================
    // SAMPLER: Chromatic sample player with built-in ADSR
    // =========================================================================
    // Loads a single WAV and pitch-shifts it across the keyboard.
    // rootNote defines the original pitch of the sample.
    auto& sampler = chain.add<Sampler>("sampler");
    sampler.loadSample("assets/audio/piano_c4.wav");
    sampler.rootNote = 60;       // Sample is Middle C
    sampler.maxVoices = 8;
    sampler.volume = 0.7f;
    sampler.attack = 0.01f;
    sampler.decay = 0.2f;
    sampler.sustain = 0.8f;
    sampler.release = 0.5f;

    // =========================================================================
    // SAMPLEBANK + SAMPLEPLAYER: Multi-sample triggering
    // =========================================================================
    // SampleBank: storage-only operator that loads WAVs into memory
    // SamplePlayer: plays samples from a connected bank by index or name

    auto& bank = chain.add<SampleBank>("sfx");
    bank.setFolder("assets/audio/samples");  // Load all WAVs from folder

    auto& player = chain.add<SamplePlayer>("player");
    player.setBank("sfx");
    player.setVoices(8);
    player.volume = 0.6f;

    // Sequencer to trigger sampler notes
    auto& seq = chain.add<Sequencer>("seq");
    seq.setTriggerSource("clock");
    seq.setStep(0,  {.velocity = 0.9f});
    seq.setStep(4,  {.velocity = 0.7f});
    seq.setStep(8,  {.velocity = 0.8f});
    seq.setStep(12, {.velocity = 0.6f});

    // Mix both samplers together
    auto& mixer = chain.add<AudioMixer>("mixer");
    mixer.input(0, "sampler");
    mixer.input(1, "player");
    mixer.gain(0, 1.0f);
    mixer.gain(1, 0.6f);

    // Reverb for space
    auto& reverb = chain.add<Reverb>("reverb");
    reverb.input("mixer");
    reverb.roomSize = 0.5f;
    reverb.damping = 0.4f;
    reverb.mix = 0.2f;

    // Audio output
    auto& out = chain.add<AudioOutput>("out");
    out.setInput("reverb");
    chain.audioOutput("out");

    // Visual output
    auto& visual = chain.add<Noise>("visual");
    visual.scale = 4.0f;
    chain.output("visual");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    auto& sampler = chain.get<Sampler>("sampler");
    auto& player = chain.get<SamplePlayer>("player");
    auto& seq = chain.get<Sequencer>("seq");
    auto& visual = chain.get<Noise>("visual");

    // On each sequencer trigger, play the next arpeggio note on sampler
    // and trigger a random sample on the player
    if (seq.triggered()) {
        float vel = seq.currentVelocity();

        // Release previous note, play next in arpeggio
        if (prevNote >= 0) {
            sampler.noteOff(prevNote);
        }
        uint8_t note = arpNotes[arpIndex];
        sampler.noteOn(note, vel);
        prevNote = note;
        arpIndex = (arpIndex + 1) % 6;

        // Trigger a sample from the bank (cycle through available samples)
        static int sampleIdx = 0;
        player.trigger(sampleIdx, vel * 0.5f, 0.0f, 1.0f);
        sampleIdx = (sampleIdx + 1) % 4;  // Cycle first 4 samples

        visual.scale = 2.0f;
    } else {
        visual.scale = static_cast<float>(visual.scale) +
                       (4.0f - static_cast<float>(visual.scale)) * 0.05f;
    }

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
