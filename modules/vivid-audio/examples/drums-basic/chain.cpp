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

    // Basic 4-on-floor pattern
    auto& kickSeq = chain.add<Sequencer>("kickSeq");
    kickSeq.setTriggerSource("clock");
    kickSeq.setPattern(0x1111);  // Every 4 steps

    auto& snareSeq = chain.add<Sequencer>("snareSeq");
    snareSeq.setTriggerSource("clock");
    snareSeq.setPattern(0x0808);  // Beats 2 and 4

    auto& hatSeq = chain.add<Sequencer>("hatSeq");
    hatSeq.setTriggerSource("clock");
    hatSeq.setPattern(0xAAAA);  // Every 2 steps (8ths)

    auto& clapSeq = chain.add<Sequencer>("clapSeq");
    clapSeq.setTriggerSource("clock");
    clapSeq.setPattern(0x0808);  // With snare

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
