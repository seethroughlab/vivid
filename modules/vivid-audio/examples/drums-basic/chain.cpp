/**
 * Drums Basic Example
 *
 * Demonstrates: Kick, Snare, HiHat, Clap
 *
 * Shows drum synthesis without UI or complex patterns.
 * Each drum is configured with its key parameters.
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

    // 808-style kick drum
    auto& kick = chain.add<Kick>("kick");
    kick.pitch = 50.0f;         // Low fundamental
    kick.pitchEnv = 150.0f;     // Pitch sweep
    kick.decay = 0.4f;          // Medium decay
    kick.click = 0.3f;          // Transient click
    kick.drive = 0.2f;          // Slight saturation

    // Snare with noise and tone
    auto& snare = chain.add<Snare>("snare");
    snare.pitch = 180.0f;       // Body pitch
    snare.tone = 0.4f;          // Tone amount
    snare.noise = 0.8f;         // Noise amount
    snare.snappy = 0.6f;        // Snare wire emphasis
    snare.toneDecay = 0.08f;
    snare.noiseDecay = 0.15f;

    // Hi-hat
    auto& hat = chain.add<HiHat>("hat");
    hat.decay = 60.0f;          // Short closed hat
    hat.tone = 0.5f;            // Metallic character
    hat.volume = 0.5f;

    // Hand clap
    auto& clap = chain.add<Clap>("clap");
    clap.decay = 0.25f;
    clap.tone = 0.6f;
    clap.sloppy = 0.4f;         // Timing spread
    clap.tail = 0.3f;           // Reverb-like tail

    // 4-on-floor pattern with velocity accents and ghost notes
    auto& kickSeq = chain.add<Sequencer>("kickSeq");
    kickSeq.setTriggerSource("clock");
    kickSeq.setStep(0,  {.velocity = 1.0f});                          // Downbeat accent
    kickSeq.setStep(4,  {.velocity = 0.85f});
    kickSeq.setStep(8,  {.velocity = 0.9f});
    kickSeq.setStep(12, {.velocity = 0.8f});
    kickSeq.setStep(14, {.velocity = 0.35f, .probability = 0.5f});    // Ghost fill

    auto& snareSeq = chain.add<Sequencer>("snareSeq");
    snareSeq.setTriggerSource("clock");
    snareSeq.setStep(3,  {.velocity = 1.0f});                         // Backbeat
    snareSeq.setStep(11, {.velocity = 0.95f});
    snareSeq.setStep(7,  {.velocity = 0.3f, .probability = 0.4f,      // Ghost note
                          .microTiming = -0.1f});                      // Slight drag

    auto& hatSeq = chain.add<Sequencer>("hatSeq");
    hatSeq.setTriggerSource("clock");
    hatSeq.setStep(0,  {.velocity = 0.8f});                           // 8th note pattern
    hatSeq.setStep(2,  {.velocity = 0.5f, .microTiming = 0.05f});     // with swing feel
    hatSeq.setStep(4,  {.velocity = 0.9f});                           // Accent
    hatSeq.setStep(6,  {.velocity = 0.5f, .microTiming = 0.05f});
    hatSeq.setStep(8,  {.velocity = 0.8f});
    hatSeq.setStep(10, {.velocity = 0.5f, .microTiming = 0.05f});
    hatSeq.setStep(12, {.velocity = 0.9f});                           // Accent
    hatSeq.setStep(14, {.velocity = 0.5f, .microTiming = 0.05f});

    auto& clapSeq = chain.add<Sequencer>("clapSeq");
    clapSeq.setTriggerSource("clock");
    clapSeq.setStep(3,  {.velocity = 0.9f});
    clapSeq.setStep(11, {.velocity = 0.85f,
                         .condition = StepCondition::TwoInThree});     // Drops out sometimes

    // Connect drums to sequencers
    kick.setTriggerSource("kickSeq");
    snare.setTriggerSource("snareSeq");
    hat.setTriggerSource("hatSeq");
    clap.setTriggerSource("clapSeq");

    // Visual feedback
    auto& visual = chain.add<Noise>("visual");
    visual.scale = 4.0f;

    chain.output("visual");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    // Visual feedback on kick
    auto& kickSeq = chain.get<Sequencer>("kickSeq");
    auto& visual = chain.get<Noise>("visual");

    if (kickSeq.triggered()) {
        visual.scale = 2.0f;
    } else {
        visual.scale = static_cast<float>(visual.scale) + (4.0f - static_cast<float>(visual.scale)) * 0.1f;
    }

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
