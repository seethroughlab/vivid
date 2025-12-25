// Direct .dspreset loading test - no JSON conversion needed
// Loads lapsteel articulation directly from XML preset
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

// Simple melody
static const int notes[] = {64, 66, 68, 71, 68, 66, 64, 62};
static const int numNotes = 8;
static int noteIndex = 0;
static float noteTime = 0.0f;
static const float noteDuration = 0.4f;
static int currentNote = -1;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Load directly from .dspreset (no JSON conversion needed!)
    auto& lapsteel = chain.add<MultiSampler>("lapsteel");
    lapsteel.loadDspreset("assets/sample_packs/lapsteel-articulations/open.dspreset");
    lapsteel.volume = 0.9f;
    lapsteel.attack = 0.02f;
    lapsteel.release = 1.0f;

    // Add reverb
    auto& reverb = chain.add<Reverb>("reverb");
    reverb.input("lapsteel");
    reverb.roomSize = 0.7f;
    reverb.damping = 0.3f;
    reverb.mix = 0.3f;

    auto& audioOut = chain.add<AudioOutput>("audioOut");
    audioOut.setInput("reverb");
    audioOut.setVolume(0.7f);
    chain.audioOutput("audioOut");

    // Simple visual
    auto& bg = chain.add<SolidColor>("bg");
    bg.color.set(0.1f, 0.15f, 0.2f, 1.0f);

    auto& levels = chain.add<Levels>("levels");
    levels.input("reverb");

    chain.output("bg");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float dt = ctx.dt();

    auto& lapsteel = chain.get<MultiSampler>("lapsteel");
    auto& levels = chain.get<Levels>("levels");
    auto& bg = chain.get<SolidColor>("bg");

    // Play melody
    noteTime += dt;
    if (noteTime >= noteDuration) {
        noteTime = 0.0f;

        // Release previous note
        if (currentNote >= 0) {
            lapsteel.noteOff(currentNote);
        }

        // Play next note
        currentNote = notes[noteIndex];
        lapsteel.noteOn(currentNote, 0.7f);
        noteIndex = (noteIndex + 1) % numNotes;
    }

    // Color responds to audio
    float level = levels.peak();
    bg.color.set(0.1f + level * 0.2f, 0.15f + level * 0.1f, 0.2f + level * 0.3f, 1.0f);

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
