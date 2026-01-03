// TapeEffect test - verify wow/flutter/saturation/hiss
// Plays a haunting minor key melody through vintage tape emulation
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <vivid/audio/notes.h>
#include <vivid/audio_output.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;
using namespace vivid::audio::freq;

// A minor melody - haunting, nostalgic (BoC-inspired)
// Am arpeggio with descending passage
static const float melody[] = {
    A3,   // Root
    C4,   // Minor 3rd
    E4,   // 5th
    A4,   // Octave
    E4,   // 5th
    C4,   // Minor 3rd
    D4,   // 4th (passing tone)
    E4,   // 5th
    A3,   // Root
    G3,   // 7th
    F3,   // 6th (minor)
    E3,   // 5th below
};
static const int melodyLength = sizeof(melody) / sizeof(melody[0]);
static float noteTime = 0.0f;
static int noteIndex = 0;
static int lastNoteIndex = -1;
static const float noteDuration = 0.4f;  // seconds per note

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Oscillator for melody
    auto& osc = chain.add<Oscillator>("osc");
    osc.waveform(Waveform::Saw);
    osc.frequency = melody[0];
    osc.volume = 1.0f;

    // ADSR envelope generator (outputs 0-1 curve)
    auto& env = chain.add<Envelope>("env");
    env.attack = 0.02f;    // Quick attack
    env.decay = 0.1f;      // Short decay
    env.sustain = 0.6f;    // Medium sustain level
    env.release = 0.15f;   // Smooth release

    // Apply envelope to oscillator via AudioGain
    auto& envGain = chain.add<AudioGain>("envGain");
    envGain.input("osc");
    envGain.setGainInput("env");  // Envelope controls gain

    // TapeEffect with noticeable settings
    auto& tape = chain.add<TapeEffect>("tape");
    tape.input("envGain");  // Input from envelope-controlled gain
    tape.wow = 0.3f;       // Gentle wow for musicality
    tape.flutter = 0.2f;   // Subtle flutter
    tape.saturation = 0.4f; // Warm saturation
    tape.hiss = 0.08f;     // Vintage hiss
    tape.mix = 1.0f;

    // Audio output
    auto& audioOut = chain.add<AudioOutput>("audioOut");
    audioOut.setInput("tape");
    audioOut.setVolume(0.7f);
    chain.audioOutput("audioOut");

    // Visual: noise that reacts to the melody
    auto& vis = chain.add<Noise>("vis");
    vis.scale = 5.0f;
    vis.speed = 0.3f;

    chain.output("vis");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float dt = ctx.dt();
    float time = ctx.time();

    auto& env = chain.get<Envelope>("env");

    // Advance melody
    noteTime += dt;
    if (noteTime >= noteDuration) {
        noteTime = 0.0f;
        noteIndex = (noteIndex + 1) % melodyLength;
    }

    // Trigger envelope on new note
    if (noteIndex != lastNoteIndex) {
        env.trigger();
        lastNoteIndex = noteIndex;
    }

    // Release note near end of duration
    if (noteTime > noteDuration - 0.1f && env.stage() == EnvelopeStage::Sustain) {
        env.releaseNote();
    }

    // Smooth glide between notes (portamento)
    float targetFreq = melody[noteIndex];
    auto& osc = chain.get<Oscillator>("osc");
    float currentFreq = osc.frequency;
    osc.frequency = currentFreq + (targetFreq - currentFreq) * 0.15f;

    // Subtle wow variation
    chain.get<TapeEffect>("tape").wow = 0.25f + 0.1f * std::sin(time * 0.3f);

    // Visual reacts to envelope level
    float envLevel = env.currentValue();
    chain.get<Noise>("vis").scale = 3.0f + envLevel * 5.0f;

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
