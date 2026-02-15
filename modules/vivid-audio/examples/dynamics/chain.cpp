/**
 * Dynamics Example
 *
 * Demonstrates: Compressor, Limiter, Gate
 *
 * Shows dynamics processing for controlling audio levels:
 * - Compressor: Reduces dynamic range
 * - Limiter: Prevents clipping
 * - Gate: Removes noise below threshold
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
    clock.division(ClockDiv::Eighth);

    // Drum sources (vary in dynamics)
    auto& kick = chain.add<Kick>("kick");
    kick.pitch = 55.0f;
    kick.decay = 0.4f;
    kick.volume = 1.0f;  // Loud

    auto& snare = chain.add<Snare>("snare");
    snare.pitch = 180.0f;
    snare.volume = 0.5f;  // Quieter

    auto& hat = chain.add<HiHat>("hat");
    hat.decay = 50.0f;
    hat.volume = 0.3f;  // Softest

    // Sequencers
    auto& kickSeq = chain.add<Sequencer>("kickSeq");
    kickSeq.setTriggerSource("clock");
    kickSeq.setPattern(0x1111);

    auto& snareSeq = chain.add<Sequencer>("snareSeq");
    snareSeq.setTriggerSource("clock");
    snareSeq.setPattern(0x0808);

    auto& hatSeq = chain.add<Sequencer>("hatSeq");
    hatSeq.setTriggerSource("clock");
    hatSeq.setPattern(0xAAAA);

    kick.setTriggerSource("kickSeq");
    snare.setTriggerSource("snareSeq");
    hat.setTriggerSource("hatSeq");

    // Mix drums together
    auto& drumMix = chain.add<AudioMixer>("drumMix");
    drumMix.setInput(0, "kick");
    drumMix.setGain(0, 1.0f);
    drumMix.setInput(1, "snare");
    drumMix.setGain(1, 1.0f);
    drumMix.setInput(2, "hat");
    drumMix.setGain(2, 1.0f);

    // Gate - removes silence/noise between hits
    auto& gate = chain.add<Gate>("gate");
    gate.input("drumMix");
    gate.threshold = -40.0f;  // Gate below -40dB
    gate.attack = 0.5f;       // Fast open
    gate.hold = 20.0f;        // Hold open briefly
    gate.release = 50.0f;     // Quick close
    gate.range = -60.0f;      // Reduce to -60dB when gated
    gate.mix = 1.0f;

    // Compressor - evens out dynamics
    auto& comp = chain.add<Compressor>("comp");
    comp.input("gate");
    comp.threshold = -18.0f;  // Compress above -18dB
    comp.ratio = 4.0f;        // 4:1 compression
    comp.attack = 5.0f;       // Fast attack (catch transients)
    comp.release = 100.0f;    // Medium release
    comp.makeupGain = 6.0f;   // +6dB makeup gain
    comp.knee = 3.0f;         // Soft knee
    comp.mix = 1.0f;

    // Limiter - prevents clipping
    auto& limiter = chain.add<Limiter>("limiter");
    limiter.input("comp");
    limiter.ceiling = -0.5f;  // Limit to -0.5dB (prevent clipping)
    limiter.release = 50.0f;  // Fast release
    limiter.mix = 1.0f;

    // Visual output
    auto& visual = chain.add<Noise>("visual");
    visual.scale = 4.0f;

    chain.output("visual");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    // Get gain reduction for visual feedback
    auto& comp = chain.get<Compressor>("comp");
    auto& visual = chain.get<Noise>("visual");

    // Show compression visually
    float gr = comp.getGainReduction();  // Returns 0 to -20dB typically
    visual.scale = 4.0f + gr * 0.2f;     // Scale shrinks with compression

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
