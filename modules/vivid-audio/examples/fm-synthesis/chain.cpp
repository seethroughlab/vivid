/**
 * FM Synthesis Example
 *
 * Demonstrates: FMSynth, FMDrum
 *
 * Shows FM synthesis for melodic and percussive sounds.
 * FMSynth provides 4-operator polyphonic synthesis.
 * FMDrum provides 2-operator percussion.
 */

#include <vivid/vivid.h>

using namespace vivid;
using namespace vivid::audio;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Master clock
    auto& clock = chain.add<Clock>("clock");
    clock.bpm = 100.0f;
    clock.division(ClockDiv::Eighth);

    // 4-operator FM synth for melodic sounds
    auto& fm = chain.add<FMSynth>("fm");
    fm.loadPreset(FMPreset::EPiano);  // Classic DX7 electric piano

    // Manual configuration (alternative to presets):
    // fm.setAlgorithm(FMAlgorithm::Stack4);
    // fm.ratio1 = 1.0f;   // Carrier
    // fm.ratio2 = 2.0f;   // Modulator
    // fm.level2 = 0.7f;   // Modulation depth
    // fm.feedback = 0.2f; // Self-modulation

    // FM drum for metallic percussion
    auto& fmBell = chain.add<FMDrum>("fmBell");
    fmBell.pitch = 600.0f;
    fmBell.ratio = 2.5f;      // Harmonic ratio
    fmBell.amount = 0.6f;     // FM depth
    fmBell.feedback = 0.1f;
    fmBell.decay = 0.5f;

    // FM drum for metallic hit
    auto& fmMetal = chain.add<FMDrum>("fmMetal");
    fmMetal.pitch = 250.0f;
    fmMetal.ratio = 3.5f;     // Inharmonic ratio
    fmMetal.feedback = 0.4f;  // More feedback = more metallic
    fmMetal.amount = 0.8f;
    fmMetal.decay = 0.2f;

    // Sequencer for percussion
    auto& bellSeq = chain.add<Sequencer>("bellSeq");
    bellSeq.setTriggerSource("clock");
    bellSeq.setPattern(0x0101);  // Every 8 steps

    auto& metalSeq = chain.add<Sequencer>("metalSeq");
    metalSeq.setTriggerSource("clock");
    metalSeq.setPattern(0x2222);  // Offbeat

    fmBell.setTriggerSource("bellSeq");
    fmMetal.setTriggerSource("metalSeq");

    // Visual output
    auto& visual = chain.add<Noise>("visual");
    visual.scale = 4.0f;

    chain.output("visual");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    auto& fm = chain.get<FMSynth>("fm");
    auto& clock = chain.get<Clock>("clock");
    auto& visual = chain.get<Noise>("visual");

    // Simple arpeggio using FMSynth
    static int noteIndex = 0;
    static float notes[] = {261.63f, 329.63f, 392.0f, 523.25f}; // C4, E4, G4, C5

    if (clock.triggered()) {
        fm.allNotesOff();
        fm.noteOn(notes[noteIndex % 4]);
        noteIndex++;

        visual.scale = 2.0f;
    } else {
        visual.scale += (4.0f - visual.scale) * 0.05f;
    }

    chain.process();
}

VIVID_CHAIN(setup, update)
