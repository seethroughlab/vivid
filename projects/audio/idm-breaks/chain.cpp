// IDM Breaks - Elektron-Style Step Sequencer Showcase
//
// Demonstrates every Step struct parameter in a musical context:
//   retrigCount/retrigRate - machine-gun kick rolls
//   probability            - ghost notes on kick & snare
//   condition              - evolving hihat density, melodic phrases
//   microTiming            - dragging snare ghosts, hihat swing
//   gate                   - tight vs open hihats
//   slide                  - pitch glides on FM percussion
//   velocity               - dynamic accents across all parts
//   note                   - per-step pitch on melodic FMDrum

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

    // === CLOCK (140 BPM, 16th notes - classic breakbeat tempo) ===
    auto& clk = chain.add<Clock>("clk");
    clk.bpm = 140.0f;
    clk.division(ClockDiv::Sixteenth);

    // =========================================================================
    // KICK - syncopated with retrig fills and probabilistic ghost notes
    // =========================================================================
    auto& kickSeq = chain.add<Sequencer>("kickSeq");
    kickSeq.setTriggerSource("clk");
    kickSeq.steps = 16;

    // Main hits: syncopated pattern
    kickSeq.setStep(0,  {.velocity = 1.0f});
    kickSeq.setStep(3,  {.velocity = 0.9f});
    kickSeq.setStep(6,  {.velocity = 0.85f,
                         .retrigCount = 3, .retrigRate = 0.3f});  // Stutter roll
    kickSeq.setStep(10, {.velocity = 0.95f});

    // Fill steps: conditional + retrig for machine-gun rolls
    kickSeq.setStep(12, {.velocity = 0.8f,
                         .retrigCount = 4, .retrigRate = 0.25f,
                         .condition = StepCondition::OneInTwo});  // Every other bar
    // Ghost note: probabilistic
    kickSeq.setStep(14, {.velocity = 0.4f,
                         .probability = 0.7f});

    auto& kick = chain.add<Kick>("kick");
    kick.setTriggerSource("kickSeq");
    kick.pitch = 48.0f;
    kick.pitchEnv = 130.0f;
    kick.pitchDecay = 0.1f;
    kick.decay = 0.25f;
    kick.click = 0.35f;
    kick.drive = 0.25f;
    kick.volume = 0.85f;

    // =========================================================================
    // SNARE - backbeat with micro-timed ghost notes
    // =========================================================================
    auto& snareSeq = chain.add<Sequencer>("snareSeq");
    snareSeq.setTriggerSource("clk");
    snareSeq.steps = 16;

    // Solid backbeat hits
    snareSeq.setStep(4,  {.velocity = 1.0f});
    snareSeq.setStep(12, {.velocity = 0.95f});

    // Ghost notes: quiet, probabilistic, dragging behind the beat
    snareSeq.setStep(7,  {.velocity = 0.3f,
                          .probability = 0.4f,
                          .microTiming = -0.1f});   // Slightly late (drag)
    snareSeq.setStep(9,  {.velocity = 0.25f,
                          .probability = 0.35f,
                          .microTiming = -0.15f});  // More drag
    snareSeq.setStep(15, {.velocity = 0.35f,
                          .probability = 0.5f,
                          .microTiming = 0.05f});   // Slightly early (push)

    auto& snare = chain.add<Snare>("snare");
    snare.setTriggerSource("snareSeq");
    snare.pitch = 190.0f;
    snare.tone = 0.35f;
    snare.noise = 0.7f;
    snare.snappy = 0.55f;
    snare.toneDecay = 0.07f;
    snare.noiseDecay = 0.14f;
    snare.volume = 0.6f;

    // =========================================================================
    // HIHAT - busy 16th pattern with conditions, micro-timing, gate variation
    // =========================================================================
    auto& hatSeq = chain.add<Sequencer>("hatSeq");
    hatSeq.setTriggerSource("clk");
    hatSeq.steps = 16;

    // Dense pattern with conditional thinning for evolving density
    hatSeq.setStep(0,  {.velocity = 0.9f,  .gate = 0.3f, .microTiming = 0.0f});
    hatSeq.setStep(1,  {.velocity = 0.4f,  .gate = 0.2f, .microTiming = 0.08f,
                        .condition = StepCondition::TwoInThree});
    hatSeq.setStep(2,  {.velocity = 0.7f,  .gate = 0.25f, .microTiming = -0.05f});
    hatSeq.setStep(3,  {.velocity = 0.45f, .gate = 0.2f, .microTiming = 0.1f,
                        .condition = StepCondition::ThreeInFour});
    hatSeq.setStep(4,  {.velocity = 1.0f,  .gate = 0.8f, .microTiming = 0.0f});  // Accent: open hat
    hatSeq.setStep(5,  {.velocity = 0.35f, .gate = 0.15f, .microTiming = -0.07f,
                        .condition = StepCondition::TwoInThree});
    hatSeq.setStep(6,  {.velocity = 0.65f, .gate = 0.25f, .microTiming = 0.06f});
    hatSeq.setStep(7,  {.velocity = 0.5f,  .gate = 0.2f, .microTiming = -0.12f,
                        .condition = StepCondition::ThreeInFour});
    hatSeq.setStep(8,  {.velocity = 0.85f, .gate = 0.3f, .microTiming = 0.0f});
    hatSeq.setStep(9,  {.velocity = 0.3f,  .gate = 0.15f, .microTiming = 0.09f,
                        .condition = StepCondition::TwoInThree});
    hatSeq.setStep(10, {.velocity = 0.7f,  .gate = 0.25f, .microTiming = -0.04f});
    hatSeq.setStep(11, {.velocity = 0.4f,  .gate = 0.2f, .microTiming = 0.11f,
                        .condition = StepCondition::ThreeInFour});
    hatSeq.setStep(12, {.velocity = 0.95f, .gate = 0.7f, .microTiming = 0.0f});  // Accent: open hat
    hatSeq.setStep(13, {.velocity = 0.35f, .gate = 0.15f, .microTiming = -0.08f,
                        .condition = StepCondition::OneInTwo});
    hatSeq.setStep(14, {.velocity = 0.6f,  .gate = 0.2f, .microTiming = 0.05f});
    hatSeq.setStep(15, {.velocity = 0.5f,  .gate = 0.2f, .microTiming = -0.15f,
                        .condition = StepCondition::TwoInThree});

    auto& hat = chain.add<HiHat>("hat");
    hat.setTriggerSource("hatSeq");
    hat.tone = 0.65f;
    hat.decay = 0.06f;
    hat.volume = 0.3f;

    // =========================================================================
    // MELODIC FM PERC - pitched hits with slide and conditional phrases
    // =========================================================================
    auto& melSeq = chain.add<Sequencer>("melSeq");
    melSeq.setTriggerSource("clk");
    melSeq.steps = 16;
    melSeq.setTarget("fmPerc");  // Route MIDI notes to FMDrum

    // Sparse melodic phrase that evolves over 4-bar cycles
    melSeq.setStep(0,  {.note = 60, .velocity = 0.8f,                               // C4
                        .condition = StepCondition::OneInFour});
    melSeq.setStep(2,  {.note = 63, .velocity = 0.7f, .slide = true,                // Eb4 - slide from C
                        .condition = StepCondition::OneInFour});
    melSeq.setStep(5,  {.note = 67, .velocity = 0.75f,                              // G4
                        .condition = StepCondition::OneInTwo});
    melSeq.setStep(7,  {.note = 65, .velocity = 0.65f, .slide = true,               // F4 - slide from G
                        .condition = StepCondition::TwoInThree});
    melSeq.setStep(8,  {.note = 72, .velocity = 0.85f,                              // C5
                        .condition = StepCondition::FirstOnly});       // Only first cycle
    melSeq.setStep(11, {.note = 70, .velocity = 0.6f, .slide = true,                // Bb4 - slide from C5
                        .condition = StepCondition::OneInThree});
    melSeq.setStep(13, {.note = 58, .velocity = 0.7f,                               // Bb3
                        .condition = StepCondition::NotFirst});        // Every cycle except first

    auto& fmPerc = chain.add<FMDrum>("fmPerc");
    fmPerc.pitch = 400.0f;
    fmPerc.ratio = 2.5f;
    fmPerc.amount = 0.55f;
    fmPerc.feedback = 0.15f;
    fmPerc.decay = 0.35f;
    fmPerc.tone = 0.6f;
    fmPerc.volume = 0.5f;

    // =========================================================================
    // MIX & PROCESSING
    // =========================================================================
    auto& drums = chain.add<AudioMixer>("drums");
    drums.setInput(0, "kick");
    drums.setInput(1, "snare");
    drums.setInput(2, "hat");
    drums.setInput(3, "fmPerc");
    drums.setGain(0, 0.7f);
    drums.setGain(1, 0.6f);
    drums.setGain(2, 0.45f);
    drums.setGain(3, 0.35f);
    drums.volume = 0.75f;

    // Subtle bitcrush for lo-fi texture
    auto& crush = chain.add<Bitcrush>("crush");
    crush.input("drums");
    crush.bits = 12;
    crush.targetSampleRate = 22000.0f;
    crush.mix = 0.3f;

    auto& out = chain.add<AudioOutput>("out");
    out.input("crush");
    chain.audioOutput("out");

    // =========================================================================
    // VISUALS - minimal audio-reactive gradient
    // =========================================================================
    levels = &chain.add<Levels>("levels");
    levels->input("crush");
    levels->smoothing = 0.75f;

    grad = &chain.add<Gradient>("grad");
    grad->mode = GradientMode::Radial;
    grad->scale = 1.0f;
    grad->colorA.set(0.06f, 0.02f, 0.08f);
    grad->colorB.set(0.02f, 0.04f, 0.02f);

    chain.output("grad");
}

void update(Context& ctx) {
    float rms = levels->rms();

    float pulse = rms * 0.5f;
    grad->colorA.set(0.06f + pulse * 0.8f, 0.02f + pulse * 0.3f, 0.08f + pulse * 0.2f);
    grad->scale = 1.0f + rms * 0.4f;

    float drift = std::sin(ctx.time() * 0.15f) * 0.02f;
    grad->colorB.set(0.02f + drift, 0.04f - drift, 0.02f + drift * 0.5f);

    ctx.chain().process(ctx);
}

VIVID_CHAIN(setup, update)
