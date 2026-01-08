// Drum Synthesis - Vivid Example
// Demonstrates: Kick, Snare, HiHat, Clap, Clock, Sequencer, AudioMixer
//
// 808-style drum machine with step sequencer

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // ----- MASTER CLOCK -----
    auto& clock = chain.add<Clock>("clock");
    clock.bpm = 120.0f;
    clock.division(ClockDiv::Sixteenth);  // 16th notes
    clock.swing = 0.0f;
    clock.start();

    // ----- DRUM VOICES -----
    // 808-style synthesized drums
    auto& kick = chain.add<Kick>("kick");
    kick.pitch = 50.0f;        // Base frequency
    kick.pitchEnv = 150.0f;    // Pitch sweep amount
    kick.pitchDecay = 0.08f;   // Fast pitch decay
    kick.decay = 0.5f;         // Amplitude decay
    kick.click = 0.4f;         // Transient click
    kick.drive = 0.2f;         // Soft saturation
    kick.volume = 0.9f;

    auto& snare = chain.add<Snare>("snare");
    snare.tone = 180.0f;       // Body frequency
    snare.decay = 0.2f;        // Amplitude decay
    snare.snappy = 0.7f;       // Snare wire noise amount
    snare.volume = 0.75f;

    auto& hihat = chain.add<HiHat>("hihat");
    hihat.decay = 0.05f;       // Short decay for closed hat
    hihat.tone = 0.3f;         // Metallic character
    hihat.volume = 0.5f;

    auto& clap = chain.add<Clap>("clap");
    clap.decay = 0.15f;        // Medium decay
    clap.spread = 0.03f;       // Timing spread of multiple hits
    clap.tone = 0.5f;          // Tonal character
    clap.volume = 0.7f;

    // ----- SEQUENCERS -----
    // Each drum has its own pattern
    auto& kick_seq = chain.add<Sequencer>("kick_seq");
    kick_seq.steps = 16;
    // Classic four-on-the-floor: 1, 5, 9, 13 (0-indexed: 0, 4, 8, 12)
    kick_seq.setStep(0, true);
    kick_seq.setStep(4, true);
    kick_seq.setStep(8, true);
    kick_seq.setStep(12, true);

    auto& snare_seq = chain.add<Sequencer>("snare_seq");
    snare_seq.steps = 16;
    // Backbeat: 5, 13 (0-indexed: 4, 12)
    snare_seq.setStep(4, true);
    snare_seq.setStep(12, true);

    auto& hihat_seq = chain.add<Sequencer>("hihat_seq");
    hihat_seq.steps = 16;
    // Every 16th note
    for (int i = 0; i < 16; i++) {
        hihat_seq.setStep(i, true, (i % 2 == 0) ? 1.0f : 0.6f);  // Accent downbeats
    }

    auto& clap_seq = chain.add<Sequencer>("clap_seq");
    clap_seq.steps = 16;
    // Same as snare, slightly different timing
    clap_seq.setStep(4, true, 0.8f);
    clap_seq.setStep(12, true, 1.0f);

    // ----- MIXER -----
    auto& mixer = chain.add<AudioMixer>("mixer");
    mixer.addInput(&kick);
    mixer.addInput(&snare);
    mixer.addInput(&hihat);
    mixer.addInput(&clap);

    // ----- AUDIO OUTPUT -----
    auto& output = chain.add<AudioOutput>("audio_out");
    output.input("mixer");

    // ----- VISUALS -----
    // Beat-reactive background
    auto& bg = chain.add<SolidColor>("bg");
    bg.color.set(0.05f, 0.05f, 0.08f, 1.0f);

    // Kick visualization
    auto& kick_viz = chain.add<Shape>("kick_viz");
    kick_viz.type(ShapeType::Circle);
    kick_viz.position.set(-0.4f, 0.0f);
    kick_viz.size.set(0.15f, 0.15f);
    kick_viz.color.set(1.0f, 0.3f, 0.1f, 1.0f);
    kick_viz.softness = 0.2f;

    // Snare visualization
    auto& snare_viz = chain.add<Shape>("snare_viz");
    snare_viz.type(ShapeType::Circle);
    snare_viz.position.set(-0.13f, 0.0f);
    snare_viz.size.set(0.12f, 0.12f);
    snare_viz.color.set(1.0f, 1.0f, 0.2f, 1.0f);
    snare_viz.softness = 0.2f;

    // HiHat visualization
    auto& hat_viz = chain.add<Shape>("hat_viz");
    hat_viz.type(ShapeType::Polygon);
    hat_viz.sides = 8;
    hat_viz.position.set(0.13f, 0.0f);
    hat_viz.size.set(0.08f, 0.08f);
    hat_viz.color.set(0.8f, 0.8f, 1.0f, 1.0f);
    hat_viz.softness = 0.1f;

    // Clap visualization
    auto& clap_viz = chain.add<Shape>("clap_viz");
    clap_viz.type(ShapeType::Star);
    clap_viz.sides = 5;
    clap_viz.position.set(0.4f, 0.0f);
    clap_viz.size.set(0.1f, 0.1f);
    clap_viz.color.set(0.5f, 1.0f, 0.5f, 1.0f);
    clap_viz.softness = 0.15f;

    // Composite all visuals
    auto& comp1 = chain.add<Composite>("comp1");
    comp1.inputA("bg");
    comp1.inputB("kick_viz");
    comp1.mode(BlendMode::Add);

    auto& comp2 = chain.add<Composite>("comp2");
    comp2.inputA("comp1");
    comp2.inputB("snare_viz");
    comp2.mode(BlendMode::Add);

    auto& comp3 = chain.add<Composite>("comp3");
    comp3.inputA("comp2");
    comp3.inputB("hat_viz");
    comp3.mode(BlendMode::Add);

    auto& comp4 = chain.add<Composite>("comp4");
    comp4.inputA("comp3");
    comp4.inputB("clap_viz");
    comp4.mode(BlendMode::Add);

    // Add bloom for glow
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("comp4");
    bloom.threshold = 0.4f;
    bloom.intensity = 1.5f;
    bloom.radius = 25.0f;

    chain.output("bloom");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    // Get operators
    auto& clock = chain.get<Clock>("clock");
    auto& kick = chain.get<Kick>("kick");
    auto& snare = chain.get<Snare>("snare");
    auto& hihat = chain.get<HiHat>("hihat");
    auto& clap = chain.get<Clap>("clap");

    auto& kick_seq = chain.get<Sequencer>("kick_seq");
    auto& snare_seq = chain.get<Sequencer>("snare_seq");
    auto& hihat_seq = chain.get<Sequencer>("hihat_seq");
    auto& clap_seq = chain.get<Sequencer>("clap_seq");

    // Mouse controls
    float mouseX = ctx.mouseNorm().x * 0.5f + 0.5f;  // 0-1
    float mouseY = ctx.mouseNorm().y * 0.5f + 0.5f;  // 0-1

    // X: BPM (80-160)
    clock.bpm = 80.0f + mouseX * 80.0f;

    // Y: Swing (0-0.5)
    clock.swing = mouseY * 0.5f;

    // Process clock and advance sequencers
    if (clock.triggered()) {
        kick_seq.advance();
        snare_seq.advance();
        hihat_seq.advance();
        clap_seq.advance();

        // Trigger drums based on sequencer state
        if (kick_seq.triggered()) {
            kick.trigger();
        }
        if (snare_seq.triggered()) {
            snare.trigger();
        }
        if (hihat_seq.triggered()) {
            hihat.trigger();
        }
        if (clap_seq.triggered()) {
            clap.trigger();
        }
    }

    // Visual feedback - scale shapes based on envelope
    float kickEnv = kick.ampEnvelope();
    float snareEnv = snare.ampEnvelope();
    float hatEnv = hihat.ampEnvelope();
    float clapEnv = clap.ampEnvelope();

    auto& kick_viz = chain.get<Shape>("kick_viz");
    float kickSize = 0.08f + kickEnv * 0.15f;
    kick_viz.size.set(kickSize, kickSize);

    auto& snare_viz = chain.get<Shape>("snare_viz");
    float snareSize = 0.06f + snareEnv * 0.12f;
    snare_viz.size.set(snareSize, snareSize);

    auto& hat_viz = chain.get<Shape>("hat_viz");
    float hatSize = 0.04f + hatEnv * 0.08f;
    hat_viz.size.set(hatSize, hatSize);

    auto& clap_viz = chain.get<Shape>("clap_viz");
    float clapSize = 0.05f + clapEnv * 0.1f;
    clap_viz.size.set(clapSize, clapSize);

    // Rotate shapes for visual interest
    auto& kick_viz2 = chain.get<Shape>("kick_viz");
    // kick_viz already retrieved
    kick_viz.rotation = t * 0.2f;

    auto& hat_viz2 = chain.get<Shape>("hat_viz");
    hat_viz2.rotation = -t * 0.5f;

    auto& clap_viz2 = chain.get<Shape>("clap_viz");
    clap_viz2.rotation = t * 0.3f;

    // Color shift based on beat
    float hue = std::fmod(static_cast<float>(clock.beat()) * 0.25f, 1.0f);
    auto& bg = chain.get<SolidColor>("bg");
    bg.color.set(
        0.03f + 0.02f * std::sin(hue * 6.28f),
        0.03f + 0.02f * std::sin(hue * 6.28f + 2.09f),
        0.05f + 0.03f * std::sin(hue * 6.28f + 4.19f),
        1.0f
    );
}

VIVID_CHAIN(setup, update)
