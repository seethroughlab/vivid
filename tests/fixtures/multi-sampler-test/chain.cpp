// MultiSampler test - loads Ganer Square Piano preset
// Plays a simple chord progression using velocity layers
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

// Chord progression: C - Am - F - G
static const int chordNotes[][4] = {
    {60, 64, 67, 72},  // C major (C4, E4, G4, C5)
    {57, 60, 64, 69},  // A minor (A3, C4, E4, A4)
    {53, 57, 60, 65},  // F major (F3, A3, C4, F4)
    {55, 59, 62, 67},  // G major (G3, B3, D4, G4)
};
static const int numChords = 4;
static int chordIndex = 0;
static float chordTime = 0.0f;
static const float chordDuration = 3.0f;  // Longer for piano

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // Audio: MultiSampler piano
    // =========================================================================

    auto& piano = chain.add<MultiSampler>("piano");
    piano.loadPreset("assets/sample_packs/1781 - Ganer Square Piano/1781 Ganer Square.json");
    piano.volume = 0.8f;
    piano.attack = 0.01f;
    piano.decay = 0.2f;
    piano.sustain = 0.8f;
    piano.release = 1.5f;

    // Add reverb for ambience
    auto& reverb = chain.add<Reverb>("reverb");
    reverb.input("piano");
    reverb.roomSize = 0.6f;
    reverb.damping = 0.4f;
    reverb.mix = 0.25f;

    auto& audioOut = chain.add<AudioOutput>("audioOut");
    audioOut.setInput("reverb");
    audioOut.setVolume(0.7f);
    chain.audioOutput("audioOut");

    // =========================================================================
    // Visuals: Simple waveform display
    // =========================================================================

    auto& bg = chain.add<Gradient>("bg");
    bg.colorA.set(0.08f, 0.06f, 0.1f, 1.0f);
    bg.colorB.set(0.04f, 0.02f, 0.06f, 1.0f);
    bg.angle = 1.57f;

    auto& levels = chain.add<Levels>("levels");
    levels.input("reverb");

    auto& shape = chain.add<Shape>("meter");
    shape.type(ShapeType::Rectangle);
    shape.position.set(0.5f, 0.5f);
    shape.size.set(0.6f, 0.02f);
    shape.color.set(0.3f, 0.7f, 1.0f, 0.8f);

    auto& comp = chain.add<Composite>("comp");
    comp.inputA("bg");
    comp.inputB("meter");
    comp.mode(BlendMode::Add);

    chain.output("comp");

    // Play first chord with varying velocities
    auto& p = chain.get<MultiSampler>("piano");
    for (int i = 0; i < 4; ++i) {
        float vel = 0.5f + (i * 0.1f);  // Increasing velocity for each note
        p.noteOn(chordNotes[0][i], vel);
    }
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float dt = ctx.dt();

    auto& piano = chain.get<MultiSampler>("piano");
    auto& levels = chain.get<Levels>("levels");
    auto& meter = chain.get<Shape>("meter");

    // Update chord progression
    chordTime += dt;
    if (chordTime >= chordDuration) {
        chordTime = 0.0f;

        // Release current chord
        for (int i = 0; i < 4; ++i) {
            piano.noteOff(chordNotes[chordIndex][i]);
        }

        // Advance to next chord
        chordIndex = (chordIndex + 1) % numChords;

        // Play new chord with varying velocities
        for (int i = 0; i < 4; ++i) {
            float vel = 0.4f + (i * 0.15f);  // Bass softer, treble louder
            piano.noteOn(chordNotes[chordIndex][i], vel);
        }
    }

    // Animate meter with audio level
    float level = levels.peak();
    meter.size.set(0.1f + level * 0.6f, 0.02f);

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
