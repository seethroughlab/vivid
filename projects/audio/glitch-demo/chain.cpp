// Glitch Demo - All Effects Complete
// Glitch meta-effect (6 effects) + Stretch (granular time-stretch)

#include <vivid/vivid.h>
#include <vivid/audio/audio.h>
#include <vivid/audio_output.h>
#include <vivid/effects/gradient.h>

using namespace vivid;
using namespace vivid::audio;
using namespace vivid::effects;

Levels* levels;
Gradient* grad;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // === CLOCK (120 BPM, 16th notes) ===
    auto& clk = chain.add<Clock>("clk");
    clk.bpm = 120.0f;
    clk.division(ClockDiv::Sixteenth);

    // === SEQUENCERS (advance on audio thread) ===
    auto& kickSeq = chain.add<Sequencer>("kickSeq");
    kickSeq.setTriggerSource("clk");
    kickSeq.steps = 16;
    kickSeq.setStep(0,  {.velocity = 1.0f});
    kickSeq.setStep(6,  {.velocity = 0.8f,
                         .retrigCount = 2, .retrigRate = 0.4f});   // Stutter
    kickSeq.setStep(10, {.velocity = 0.85f});
    kickSeq.setStep(14, {.velocity = 0.4f, .probability = 0.6f}); // Ghost fill

    auto& snareSeq = chain.add<Sequencer>("snareSeq");
    snareSeq.setTriggerSource("clk");
    snareSeq.steps = 16;
    snareSeq.setStep(4,  {.velocity = 1.0f});                     // Backbeat
    snareSeq.setStep(12, {.velocity = 0.9f});
    snareSeq.setStep(9,  {.velocity = 0.3f, .probability = 0.35f, // Ghost note
                          .microTiming = -0.08f});                 // Drag

    auto& hatSeq = chain.add<Euclidean>("hatSeq");
    hatSeq.setTriggerSource("clk");
    hatSeq.steps = 16;
    hatSeq.hits = 8;  // Even 8ths

    // === DRUMS (trigger on sequencer output) ===
    auto& kick = chain.add<Kick>("kick");
    kick.setTriggerSource("kickSeq");
    kick.pitch = 55.0f;
    kick.click = 0.4f;
    kick.pitchDecay = 0.15f;
    kick.drive = 0.2f;
    kick.volume = 0.7f;

    auto& snare = chain.add<Snare>("snare");
    snare.setTriggerSource("snareSeq");
    snare.pitch = 180.0f;
    snare.snappy = 0.5f;
    snare.noise = 0.6f;
    snare.noiseDecay = 0.2f;
    snare.volume = 0.5f;

    auto& hat = chain.add<HiHat>("hat");
    hat.setTriggerSource("hatSeq");
    hat.tone = 0.7f;
    hat.decay = 0.08f;
    hat.volume = 0.25f;

    auto& drums = chain.add<AudioMixer>("drums");
    drums.setInput(0, "kick");
    drums.setInput(1, "snare");
    drums.setInput(2, "hat");
    drums.setGain(0, 1.0f);
    drums.setGain(1, 0.8f);
    drums.setGain(2, 0.6f);

    // === GLITCH META-EFFECT ===
    // Single operator replaces the 6 chained effects!
    auto& glitch = chain.add<Glitch>("glitch");
    glitch.input("drums");
    glitch.bpm = 120.0f;
    glitch.triggerDiv(ClockDiv::Quarter);

    // Set per-effect probabilities
    glitch.repeatChance = 0.2f;    // BeatRepeat
    glitch.reverseChance = 0.15f;  // Reverse
    glitch.stutterChance = 0.15f;  // Stutter
    glitch.scratchChance = 0.1f;   // Scratch
    glitch.tapeChance = 0.08f;     // TapeStop
    glitch.shiftChance = 0.1f;     // FrequencyShift
    glitch.mix = 1.0f;

    // === STRETCH (Granular Time-Stretch) ===
    auto& stretch = chain.add<Stretch>("stretch");
    stretch.input("glitch");
    stretch.bpm = 120.0f;
    stretch.triggerDiv(ClockDiv::Whole);     // Check every bar
    stretch.stretchDiv(ClockDiv::Quarter);   // Stretch a quarter note
    stretch.stretchFactor = 2.0f;            // Double the time (half speed)
    stretch.grainSize = 60.0f;               // 60ms grains
    stretch.overlap = 0.5f;
    stretch.chance = 0.15f;                  // 15% chance

    // === OUTPUT ===
    auto& out = chain.add<AudioOutput>("out");
    out.input("stretch");
    chain.audioOutput("out");

    // === VISUALS ===
    levels = &chain.add<Levels>("levels");
    levels->input("stretch");
    levels->smoothing = 0.8f;

    grad = &chain.add<Gradient>("grad");
    grad->mode = GradientMode::Radial;
    grad->scale = 1.0f;
    grad->colorA.set(0.08f, 0.04f, 0.02f);
    grad->colorB.set(0.02f, 0.02f, 0.04f);

    chain.output("grad");
}

void update(Context& ctx) {
    float t = ctx.time();

    // === Reactive visuals ===
    float rms = levels->rms();

    float pulse = rms * 0.6f;
    grad->colorA.set(0.08f + pulse, 0.04f + pulse * 0.5f, 0.02f);
    grad->scale = 1.0f + rms * 0.5f;

    float drift = std::sin(t * 0.1f) * 0.02f;
    grad->colorB.set(0.02f + drift, 0.02f, 0.04f - drift);

    ctx.chain().process(ctx);
}

VIVID_CHAIN(setup, update)
