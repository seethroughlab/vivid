/**
 * Glitch Master Example
 *
 * Demonstrates: Glitch, FrequencyShift, Stretch
 *
 * The master Glitch processor (which combines all effects internally)
 * plus standalone FrequencyShift and Stretch operators for metallic
 * and time-bending textures.
 */

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>

using namespace vivid;
using namespace vivid::audio;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Master clock
    auto& clock = chain.add<Clock>("clock");
    clock.bpm = 120.0f;
    clock.division(ClockDiv::Sixteenth);

    // Melodic source: wavetable synth playing a sequence
    auto& synth = chain.add<WavetableSynth>("synth");
    synth.loadBuiltin(BuiltinTable::Analog);
    synth.volume = 0.6f;
    synth.filterCutoff = 2000.0f;
    synth.filterResonance = 0.3f;

    auto& seq = chain.add<Sequencer>("seq");
    seq.setTriggerSource("clock");
    seq.setTarget("synth");
    seq.setStep(0,  {.note = 48, .velocity = 0.9f});
    seq.setStep(2,  {.note = 55, .velocity = 0.7f});
    seq.setStep(4,  {.note = 51, .velocity = 0.8f});
    seq.setStep(6,  {.note = 53, .velocity = 0.6f});
    seq.setStep(8,  {.note = 48, .velocity = 0.85f});
    seq.setStep(10, {.note = 60, .velocity = 0.7f});
    seq.setStep(12, {.note = 55, .velocity = 0.9f});
    seq.setStep(14, {.note = 53, .velocity = 0.5f});

    // FrequencyShift: adds metallic, inharmonic texture
    // Shifts all frequencies by a fixed Hz amount (not pitch-shifting)
    auto& freqShift = chain.add<FrequencyShift>("shift");
    freqShift.input("synth");
    freqShift.shift = 30.0f;          // Shift up 30 Hz for metallic color
    freqShift.bpm = 120.0f;
    freqShift.modDiv(ClockDiv::Quarter);
    freqShift.modDepth = 20.0f;       // LFO modulation for movement
    freqShift.mix = 0.4f;             // Blend with dry signal

    // Stretch: granular time-stretching without pitch change
    auto& stretch = chain.add<Stretch>("stretch");
    stretch.input("shift");
    stretch.bpm = 120.0f;
    stretch.triggerDiv(ClockDiv::Half);
    stretch.stretchDiv(ClockDiv::Quarter);
    stretch.stretchFactor = 2.0f;     // Half-speed stretch
    stretch.grainSize = 60.0f;        // 60ms grains
    stretch.grainRandom = 0.15f;      // Slight position randomization
    stretch.overlap = 0.5f;
    stretch.chance = 0.2f;            // 20% trigger chance
    stretch.mix = 1.0f;

    // Glitch: master processor with all effects combined
    // Only one effect plays at a time, selected by probability
    auto& glitch = chain.add<Glitch>("glitch");
    glitch.input("stretch");
    glitch.bpm = 120.0f;
    glitch.triggerDiv(ClockDiv::Quarter);
    glitch.repeatChance = 0.2f;       // BeatRepeat
    glitch.reverseChance = 0.15f;     // Reverse
    glitch.stutterChance = 0.15f;     // Stutter
    glitch.scratchChance = 0.1f;      // Scratch
    glitch.tapeChance = 0.08f;        // TapeStop
    glitch.shiftChance = 0.1f;        // FrequencyShift (internal)
    glitch.mix = 1.0f;

    // Audio output
    auto& out = chain.add<AudioOutput>("out");
    out.input("glitch");
    chain.audioOutput("out");

    // Visual feedback
    auto& visual = chain.add<Noise>("visual");
    visual.scale = 3.0f;
    chain.output("visual");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    // Slowly modulate frequency shift for evolving timbre
    auto& freqShift = chain.get<FrequencyShift>("shift");
    freqShift.shift = 20.0f + 30.0f * std::sin(t * 0.3f);

    auto& seq = chain.get<Sequencer>("seq");
    auto& visual = chain.get<Noise>("visual");

    if (seq.triggered()) {
        visual.scale = 2.0f;
    } else {
        visual.scale = static_cast<float>(visual.scale) +
                       (3.0f - static_cast<float>(visual.scale)) * 0.05f;
    }

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
