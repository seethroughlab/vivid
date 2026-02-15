/**
 * Effects Chain Example
 *
 * Demonstrates: Delay, Echo, Reverb, Chorus, Phaser, Flanger
 *
 * Shows time-based audio effects applied to a simple drum pattern.
 * Each effect is configured with typical settings.
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
    clock.bpm = 110.0f;
    clock.division(ClockDiv::Eighth);

    // Simple drum source
    auto& kick = chain.add<Kick>("kick");
    kick.pitch = 55.0f;
    kick.decay = 0.3f;

    auto& hat = chain.add<HiHat>("hat");
    hat.decay = 60.0f;
    hat.volume = 0.4f;

    auto& kickSeq = chain.add<Sequencer>("kickSeq");
    kickSeq.setTriggerSource("clock");
    kickSeq.setPattern(0x1001);  // Kick on 1 and 9

    auto& hatSeq = chain.add<Sequencer>("hatSeq");
    hatSeq.setTriggerSource("clock");
    hatSeq.setPattern(0xAAAA);  // 8th notes

    kick.setTriggerSource("kickSeq");
    hat.setTriggerSource("hatSeq");

    // Delay effect on hi-hat
    auto& delay = chain.add<Delay>("delay");
    delay.input("hat");
    delay.delayTime = 375.0f;   // Dotted eighth at 110 BPM
    delay.feedback = 0.4f;      // Moderate feedback
    delay.mix = 0.3f;

    // Echo effect (multi-tap delay)
    auto& echo = chain.add<Echo>("echo");
    echo.input("kick");
    echo.delayTime = 273.0f;    // Eighth note at 110 BPM
    echo.decay = 0.5f;          // Each tap at 50% of previous
    echo.taps = 3;              // 3 echoes
    echo.mix = 0.25f;

    // Reverb for space
    auto& reverb = chain.add<Reverb>("reverb");
    reverb.input("delay");
    reverb.roomSize = 0.6f;     // Medium room
    reverb.damping = 0.4f;      // Some high-freq absorption
    reverb.width = 1.0f;        // Full stereo
    reverb.mix = 0.25f;

    // Chorus for thickness
    auto& chorus = chain.add<Chorus>("chorus");
    chorus.input("reverb");
    chorus.rate = 0.4f;         // Slow LFO
    chorus.depth = 4.0f;        // 4ms modulation depth
    chorus.voices = 2;          // 2 chorus voices
    chorus.mix = 0.3f;

    // Phaser for movement
    auto& phaser = chain.add<Phaser>("phaser");
    phaser.input("echo");
    phaser.rate = 0.2f;         // Slow sweep
    phaser.depth = 0.7f;        // Deep modulation
    phaser.stages = 6;          // 6-stage phaser
    phaser.feedback = 0.4f;
    phaser.mix = 0.4f;

    // Flanger for jet effect (applied to final mix)
    auto& flanger = chain.add<Flanger>("flanger");
    flanger.input("phaser");
    flanger.rate = 0.15f;       // Very slow sweep
    flanger.depth = 0.5f;
    flanger.feedback = 0.3f;
    flanger.mix = 0.2f;         // Subtle flanging

    // Visual output
    auto& visual = chain.add<Noise>("visual");
    visual.scale = 4.0f;

    chain.output("visual");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    auto& kickSeq = chain.get<Sequencer>("kickSeq");
    auto& visual = chain.get<Noise>("visual");

    if (kickSeq.triggered()) {
        visual.scale = 2.0f;
    } else {
        visual.scale = static_cast<float>(visual.scale) + (4.0f - static_cast<float>(visual.scale)) * 0.05f;
    }

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
