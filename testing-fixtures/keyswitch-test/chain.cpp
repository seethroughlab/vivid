// Keyswitch test - tests articulation switching with lapsteel samples
// Press keyboard 1/2/3 to switch articulations, plays notes automatically
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

// Melody notes (in lapsteel range: ~52-81)
static const int melodyNotes[] = {64, 66, 68, 71, 73, 71, 68, 66};
static const int numNotes = 8;
static int noteIndex = 0;
static float noteTime = 0.0f;
static const float noteDuration = 0.5f;
static int currentNote = -1;
static int currentArticulation = 0;  // 0=open, 1=slide-down, 2=slide-up
static float articulationTime = 0.0f;
static const float articulationDuration = 4.0f;  // Switch every 4 seconds

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // Audio: MultiSampler with keyswitches
    // =========================================================================

    auto& lapsteel = chain.add<MultiSampler>("lapsteel");
    lapsteel.loadPreset("assets/sample_packs/lapsteel-articulations/lapsteel-combined.json");
    lapsteel.volume = 0.9f;
    lapsteel.attack = 0.02f;
    lapsteel.release = 0.8f;

    // Add delay for ambience
    auto& delay = chain.add<Delay>("delay");
    delay.input("lapsteel");
    delay.delayTime = 300.0f;  // ms
    delay.feedback = 0.3f;
    delay.mix = 0.2f;

    auto& audioOut = chain.add<AudioOutput>("audioOut");
    audioOut.setInput("delay");
    audioOut.setVolume(0.8f);
    chain.audioOutput("audioOut");

    // =========================================================================
    // Visuals: Show current articulation
    // =========================================================================

    auto& bg = chain.add<Gradient>("bg");
    bg.colorA.set(0.1f, 0.08f, 0.06f, 1.0f);
    bg.colorB.set(0.05f, 0.04f, 0.03f, 1.0f);
    bg.angle = 1.57f;

    // Articulation indicator
    auto& indicator = chain.add<Shape>("indicator");
    indicator.type(ShapeType::Rectangle);
    indicator.position.set(0.5f, 0.5f);
    indicator.size.set(0.3f, 0.1f);
    indicator.color.set(0.8f, 0.6f, 0.2f, 0.9f);

    auto& comp = chain.add<Composite>("comp");
    comp.inputA("bg");
    comp.inputB("indicator");
    comp.mode(BlendMode::Add);

    chain.output("comp");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float dt = ctx.dt();

    auto& lapsteel = chain.get<MultiSampler>("lapsteel");
    auto& indicator = chain.get<Shape>("indicator");

    // Auto-cycle articulations
    articulationTime += dt;
    if (articulationTime >= articulationDuration) {
        articulationTime = 0.0f;
        currentArticulation = (currentArticulation + 1) % 3;

        // Send keyswitch (C0=24 for Open, C#0=25 for Slide Down, D0=26 for Slide Up)
        lapsteel.setKeyswitch(24 + currentArticulation);
    }

    // Update indicator color based on articulation
    switch (currentArticulation) {
        case 0:  // Open - warm orange
            indicator.color.set(0.9f, 0.6f, 0.2f, 0.9f);
            break;
        case 1:  // Slide Down - blue
            indicator.color.set(0.2f, 0.5f, 0.9f, 0.9f);
            break;
        case 2:  // Slide Up - green
            indicator.color.set(0.3f, 0.8f, 0.4f, 0.9f);
            break;
    }

    // Play melody
    noteTime += dt;
    if (noteTime >= noteDuration) {
        noteTime = 0.0f;

        // Release previous note
        if (currentNote >= 0) {
            lapsteel.noteOff(currentNote);
        }

        // Play next note
        currentNote = melodyNotes[noteIndex];
        lapsteel.noteOn(currentNote, 0.7f);
        noteIndex = (noteIndex + 1) % numNotes;

        // Pulse the indicator
        indicator.size.set(0.35f, 0.12f);
    } else {
        // Shrink back
        float t = noteTime / noteDuration;
        float size = 0.3f + 0.05f * (1.0f - t);
        indicator.size.set(size, 0.1f);
    }

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
