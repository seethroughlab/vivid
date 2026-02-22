/**
 * Glitch Effects Example
 *
 * Demonstrates: BeatRepeat, Reverse, Stutter, TapeStop, Scratch
 *
 * A drum loop processed through individual glitch effects chained
 * in series, each with low probability so they trigger independently.
 */

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>

using namespace vivid;
using namespace vivid::audio;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Master clock at 128 BPM, sixteenth-note resolution
    auto& clock = chain.add<Clock>("clock");
    clock.bpm = 128.0f;
    clock.division(ClockDiv::Sixteenth);

    // Kick drum pattern: four-on-the-floor with accent
    auto& kickSeq = chain.add<Sequencer>("kickSeq");
    kickSeq.setTriggerSource("clock");
    kickSeq.setStep(0,  {.velocity = 1.0f});
    kickSeq.setStep(4,  {.velocity = 0.9f});
    kickSeq.setStep(8,  {.velocity = 0.95f});
    kickSeq.setStep(12, {.velocity = 0.85f});

    auto& kick = chain.add<Kick>("kick");
    kick.setTriggerSource("kickSeq");
    kick.pitch = 50.0f;
    kick.decay = 0.3f;

    // Snare on beats 2 and 4
    auto& snareSeq = chain.add<Sequencer>("snareSeq");
    snareSeq.setTriggerSource("clock");
    snareSeq.setStep(4,  {.velocity = 0.9f});
    snareSeq.setStep(12, {.velocity = 1.0f});

    auto& snare = chain.add<Snare>("snare");
    snare.setTriggerSource("snareSeq");
    snare.toneDecay = 0.1f;
    snare.noiseDecay = 0.15f;
    snare.tone = 0.5f;

    // Hi-hats on eighth notes
    auto& hatSeq = chain.add<Sequencer>("hatSeq");
    hatSeq.setTriggerSource("clock");
    hatSeq.setStep(2,  {.velocity = 0.6f});
    hatSeq.setStep(6,  {.velocity = 0.5f});
    hatSeq.setStep(10, {.velocity = 0.7f});
    hatSeq.setStep(14, {.velocity = 0.5f});

    auto& hat = chain.add<HiHat>("hat");
    hat.setTriggerSource("hatSeq");
    hat.decay = 60.0f;
    hat.volume = 0.4f;

    // Mix drums together
    auto& mixer = chain.add<AudioMixer>("mixer");
    mixer.input(0, "kick");
    mixer.input(1, "snare");
    mixer.input(2, "hat");
    mixer.gain(0, 1.0f);
    mixer.gain(1, 0.8f);
    mixer.gain(2, 0.5f);

    // --- Glitch effects chain (low probability, trigger independently) ---

    // BeatRepeat: captures and loops short slices
    auto& repeat = chain.add<BeatRepeat>("repeat");
    repeat.input("mixer");
    repeat.bpm = 128.0f;
    repeat.triggerDiv(ClockDiv::Eighth);
    repeat.sliceDiv(ClockDiv::Sixteenth);
    repeat.chance = 0.15f;
    repeat.repeatCount = 4;
    repeat.decay = 0.2f;
    repeat.mix = 1.0f;

    // Reverse: plays audio slices backwards
    auto& rev = chain.add<Reverse>("reverse");
    rev.input("repeat");
    rev.bpm = 128.0f;
    rev.triggerDiv(ClockDiv::Half);
    rev.reverseDiv(ClockDiv::Quarter);
    rev.chance = 0.12f;
    rev.mix = 1.0f;

    // Stutter: rhythmic micro-repeats with volume envelope
    auto& stutter = chain.add<Stutter>("stutter");
    stutter.input("reverse");
    stutter.bpm = 128.0f;
    stutter.triggerDiv(ClockDiv::Half);
    stutter.stutterDiv(ClockDiv::ThirtySecond);
    stutter.stutterCount = 8;
    stutter.envelope(StutterEnvelope::Decay);
    stutter.chance = 0.1f;
    stutter.mix = 1.0f;

    // TapeStop: pitch drops like a tape deck stopping
    auto& tape = chain.add<TapeStop>("tape");
    tape.input("stutter");
    tape.bpm = 128.0f;
    tape.triggerDiv(ClockDiv::Whole);
    tape.mode(TapeMode::StopStart);
    tape.stopTime = 400.0f;
    tape.startTime = 150.0f;
    tape.chance = 0.1f;
    tape.mix = 1.0f;

    // Scratch: DJ-style variable-speed playback
    auto& scratch = chain.add<Scratch>("scratch");
    scratch.input("tape");
    scratch.bpm = 128.0f;
    scratch.triggerDiv(ClockDiv::Half);
    scratch.motion(ScratchMotion::BackForth);
    scratch.speed = 1.0f;
    scratch.speedRandom = 0.4f;
    scratch.scratchBeats = 0.5f;
    scratch.chance = 0.1f;
    scratch.mix = 1.0f;

    // Audio output
    auto& out = chain.add<AudioOutput>("out");
    out.input("scratch");
    chain.audioOutput("out");

    // Visual feedback
    auto& visual = chain.add<Noise>("visual");
    visual.scale = 4.0f;
    chain.output("visual");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& kickSeq = chain.get<Sequencer>("kickSeq");
    auto& visual = chain.get<Noise>("visual");

    // Pulse visual on kick hits
    if (kickSeq.triggered()) {
        visual.scale = 2.0f;
    } else {
        visual.scale = static_cast<float>(visual.scale) +
                       (4.0f - static_cast<float>(visual.scale)) * 0.05f;
    }

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
