// Trigger Callback Test
//
// Demonstrates using onTrigger() callbacks to sync audio and visuals
// without manual polling in update()

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <vivid/audio_output.h>
#include <iostream>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // Audio setup
    // =========================================================================

    auto& clock = chain.add<Clock>("clock");
    clock.bpm = 120.0f;
    clock.division(ClockDiv::Sixteenth);

    // Kick on 1, 5, 9, 13
    auto& kickSeq = chain.add<Sequencer>("kickSeq");
    kickSeq.steps = 16;
    kickSeq.setPattern(0b0001000100010001);

    auto& kick = chain.add<Kick>("kick");

    // Snare on 5, 13
    auto& snareSeq = chain.add<Sequencer>("snareSeq");
    snareSeq.steps = 16;
    snareSeq.setPattern(0b0001000000010000);

    auto& snare = chain.add<Snare>("snare");

    // Hi-hat euclidean pattern
    auto& hatSeq = chain.add<Euclidean>("hatSeq");
    hatSeq.steps = 16;
    hatSeq.hits = 7;

    auto& hihat = chain.add<HiHat>("hihat");

    // Mix and output
    auto& mixer = chain.add<AudioMixer>("mixer");
    mixer.setInput(0, "kick");
    mixer.setGain(0, 0.8f);
    mixer.setInput(1, "snare");
    mixer.setGain(1, 0.6f);
    mixer.setInput(2, "hihat");
    mixer.setGain(2, 0.4f);

    auto& audioOut = chain.add<AudioOutput>("audioOut");
    audioOut.setInput("mixer");
    chain.audioOutput("audioOut");

    // =========================================================================
    // Visual setup
    // =========================================================================

    auto& noise = chain.add<Noise>("noise");
    noise.scale = 4.0f;
    noise.octaves = 3;

    // Flash for each drum
    auto& kickFlash = chain.add<Flash>("kickFlash");
    kickFlash.input("noise");
    kickFlash.decay = 0.82f;
    kickFlash.color.set(1.0f, 1.0f, 1.0f);
    kickFlash.mode = 0;  // Additive

    auto& snareFlash = chain.add<Flash>("snareFlash");
    snareFlash.input("kickFlash");
    snareFlash.decay = 0.90f;
    snareFlash.color.set(1.0f, 0.5f, 0.2f);
    snareFlash.mode = 1;  // Screen

    auto& hatFlash = chain.add<Flash>("hatFlash");
    hatFlash.input("snareFlash");
    hatFlash.decay = 0.75f;
    hatFlash.color.set(0.3f, 0.8f, 1.0f);
    hatFlash.mode = 0;  // Additive

    // Particles for kick
    auto& particles = chain.add<Particles>("particles");
    particles.emitter(EmitterShape::Disc);
    particles.position(0.5f, 0.5f);
    particles.emitterSize(0.1f);
    particles.emitRate(0.0f);  // Only emit on trigger
    particles.maxParticles(200);
    particles.radialVelocity(0.5f);
    particles.spread(360.0f);
    particles.life(1.0f);
    particles.size(0.02f, 0.005f);
    particles.color(1.0f, 0.9f, 0.7f, 1.0f);
    particles.colorEnd(1.0f, 0.3f, 0.0f, 0.0f);
    particles.clearColor(0.0f, 0.0f, 0.0f, 0.0f);

    // Composite particles over flashes
    auto& comp = chain.add<Composite>("comp");
    comp.inputA("hatFlash");
    comp.inputB("particles");
    comp.mode(BlendMode::Add);

    chain.output("comp");

    // =========================================================================
    // Trigger callbacks - the key feature!
    // =========================================================================
    // IMPORTANT: Capture chain pointer, not local references (they go out of scope)
    auto* chainPtr = &chain;

    // Kick triggers: flash + particle burst
    kickSeq.onTrigger([chainPtr](float velocity) {
        chainPtr->get<Kick>("kick").trigger();
        chainPtr->get<Flash>("kickFlash").trigger(velocity);
        chainPtr->get<Particles>("particles").burst(static_cast<int>(30 * velocity));
    });

    // Snare triggers: audio + flash
    snareSeq.onTrigger([chainPtr](float velocity) {
        chainPtr->get<Snare>("snare").trigger();
        chainPtr->get<Flash>("snareFlash").trigger(velocity);
    });

    // Hat triggers (Euclidean - no velocity)
    hatSeq.onTrigger([chainPtr]() {
        chainPtr->get<HiHat>("hihat").trigger();
        chainPtr->get<Flash>("hatFlash").trigger(0.4f);
    });

    std::cout << "\n";
    std::cout << "Trigger Callback Test\n";
    std::cout << "=====================\n";
    std::cout << "Audio and visuals are synced via onTrigger() callbacks\n";
    std::cout << "No manual polling needed in update()!\n";
    std::cout << "\n";
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    // Advance clock
    auto& clock = chain.get<Clock>("clock");

    // Advance sequencers when clock ticks
    // Callbacks automatically fire when steps trigger!
    if (clock.triggered()) {
        chain.get<Sequencer>("kickSeq").advance();
        chain.get<Sequencer>("snareSeq").advance();
        chain.get<Euclidean>("hatSeq").advance();
    }

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
