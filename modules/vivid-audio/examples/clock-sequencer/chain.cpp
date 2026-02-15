/**
 * Clock & Sequencer Example
 *
 * Demonstrates: Clock, Sequencer, Euclidean
 *
 * Shows basic timing and trigger patterns:
 * - Clock generates BPM-synced triggers
 * - Sequencer creates step-based patterns
 * - Euclidean generates distributed rhythms
 */

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>

using namespace vivid;
using namespace vivid::audio;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Master clock at 120 BPM
    auto& clock = chain.add<Clock>("clock");
    clock.bpm = 120.0f;
    clock.division(ClockDiv::Sixteenth);

    // Step sequencer for kick pattern
    auto& kickSeq = chain.add<Sequencer>("kickSeq");
    kickSeq.setTriggerSource("clock");
    kickSeq.setPattern(0x1111);  // Hits on 1, 5, 9, 13

    // Euclidean rhythm for hi-hat (5 hits over 16 steps)
    auto& hatEucl = chain.add<Euclidean>("hatEucl");
    hatEucl.setTriggerSource("clock");
    hatEucl.steps = 16;
    hatEucl.hits = 5;

    // Drums triggered by patterns
    auto& kick = chain.add<Kick>("kick");
    kick.setTriggerSource("kickSeq");
    kick.decay = 200.0f;
    kick.pitch = 50.0f;

    auto& hat = chain.add<HiHat>("hat");
    hat.setTriggerSource("hatEucl");
    hat.decay = 80.0f;
    hat.tone = 0.6f;

    // Visual feedback
    auto& visual = chain.add<Noise>("visual");
    visual.scale = 4.0f;

    chain.output("visual");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    auto& kickSeq = chain.get<Sequencer>("kickSeq");
    auto& hatEucl = chain.get<Euclidean>("hatEucl");
    auto& visual = chain.get<Noise>("visual");

    // Flash visuals on triggers
    if (kickSeq.triggered()) {
        visual.scale = 2.0f;
    } else if (hatEucl.triggered()) {
        visual.scale = 3.0f;
    } else {
        visual.scale = 4.0f + std::sin(ctx.time()) * 0.5f;
    }

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
