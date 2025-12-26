// WavetableSynth Phase 1 Features Test
// Tests: unison, sub oscillator, portamento, velocity sensitivity

#include <vivid/vivid.h>
#include <vivid/audio/audio.h>
#include <vivid/audio_output.h>

using namespace vivid;
using namespace vivid::audio;

WavetableSynth* synth;
int frame = 0;

void setup(Context& ctx) {
    // Create wavetable synth with enhanced features
    synth = &ctx.chain().add<WavetableSynth>("wt");

    // Load analog wavetable for rich sound
    synth->loadBuiltin(BuiltinTable::Analog);

    // === UNISON SETTINGS ===
    synth->unisonVoices = 4;        // 4 voices per note
    synth->unisonSpread = 25.0f;    // 25 cents spread
    synth->unisonStereo = 0.8f;     // Wide stereo

    // === SUB OSCILLATOR ===
    synth->subLevel = 0.3f;         // 30% sub level
    synth->subOctave = -1;          // One octave below

    // === PORTAMENTO ===
    synth->portamento = 200.0f;     // 200ms glide

    // === VELOCITY SENSITIVITY ===
    synth->velToVolume = 0.5f;      // 50% velocity to volume
    synth->velToAttack = 0.3f;      // High velocity = faster attack

    // Envelope settings
    synth->attack = 0.02f;
    synth->decay = 0.3f;
    synth->sustain = 0.6f;
    synth->release = 0.5f;
    synth->volume = 0.4f;

    // Position for wavetable morph
    synth->position = 0.3f;

    // Output to speakers
    ctx.chain().add<AudioOutput>("out");
}

void update(Context& ctx) {
    frame++;

    // Play a sequence demonstrating features
    int fps = 60;

    // Frame 30: First note with high velocity
    if (frame == 30) {
        synth->noteOnMidi(48, 120);  // C3, high velocity
    }

    // Frame 90: Release and play next note (portamento will glide)
    if (frame == 90) {
        synth->noteOffMidi(48);
    }

    // Frame 100: Second note with medium velocity
    if (frame == 100) {
        synth->noteOnMidi(55, 80);   // G3, medium velocity
    }

    // Frame 160: Release and play chord
    if (frame == 160) {
        synth->noteOffMidi(55);
    }

    // Frame 170: Chord with varying velocities
    if (frame == 170) {
        synth->noteOnMidi(48, 100);  // C3
        synth->noteOnMidi(52, 70);   // E3, softer
        synth->noteOnMidi(55, 90);   // G3
    }

    // Frame 270: Release chord
    if (frame == 270) {
        synth->allNotesOff();
    }

    // Slowly morph wavetable position
    synth->position = 0.3f + 0.2f * std::sin(static_cast<float>(frame) * 0.02f);

    ctx.chain().process(ctx);
}

VIVID_CHAIN(setup, update)
