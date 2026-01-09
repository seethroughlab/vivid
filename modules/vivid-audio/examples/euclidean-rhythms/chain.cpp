// Euclidean Rhythms - Vivid Example
// Demonstrates: Euclidean, Clock
//
// Polyrhythmic patterns using the Euclidean algorithm

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
    clock.division(ClockDiv::Sixteenth);
    clock.start();

    // ----- DRUMS -----
    auto& kick = chain.add<Kick>("kick");
    kick.pitch = 45.0f;
    kick.pitchEnv = 120.0f;
    kick.decay = 0.4f;
    kick.click = 0.3f;
    kick.volume = 0.85f;

    auto& snare = chain.add<Snare>("snare");
    snare.tone = 200.0f;
    snare.decay = 0.15f;
    snare.snappy = 0.6f;
    snare.volume = 0.7f;

    auto& hihat = chain.add<HiHat>("hihat");
    hihat.decay = 0.03f;
    hihat.tone = 0.4f;
    hihat.volume = 0.4f;

    auto& clap = chain.add<Clap>("clap");
    clap.decay = 0.12f;
    clap.spread = 0.02f;
    clap.volume = 0.6f;

    // ----- EUCLIDEAN PATTERNS -----
    // Each uses different steps/hits for polyrhythm

    // E(4,16) - Four on the floor kick
    auto& kick_eucl = chain.add<Euclidean>("kick_eucl");
    kick_eucl.steps = 16;
    kick_eucl.hits = 4;
    kick_eucl.rotation = 0;

    // E(3,8) - Tresillo (classic Cuban rhythm)
    auto& snare_eucl = chain.add<Euclidean>("snare_eucl");
    snare_eucl.steps = 8;
    snare_eucl.hits = 3;
    snare_eucl.rotation = 0;

    // E(5,16) - Bossa nova hat pattern
    auto& hat_eucl = chain.add<Euclidean>("hat_eucl");
    hat_eucl.steps = 16;
    hat_eucl.hits = 5;
    hat_eucl.rotation = 0;

    // E(7,16) - Samba-inspired clap
    auto& clap_eucl = chain.add<Euclidean>("clap_eucl");
    clap_eucl.steps = 16;
    clap_eucl.hits = 7;
    clap_eucl.rotation = 2;  // Offset for groove

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
    // Background
    auto& bg = chain.add<SolidColor>("bg");
    bg.color.set(0.03f, 0.03f, 0.06f, 1.0f);

    // Ring visualizations for each euclidean pattern
    // Kick ring (large, center)
    auto& kick_ring = chain.add<Shape>("kick_ring");
    kick_ring.type = ShapeType::Ring;
    kick_ring.size.set(0.35f, 0.35f);
    kick_ring.thickness = 0.03f;
    kick_ring.color.set(1.0f, 0.3f, 0.1f, 1.0f);

    // Snare ring
    auto& snare_ring = chain.add<Shape>("snare_ring");
    snare_ring.type = ShapeType::Ring;
    snare_ring.size.set(0.28f, 0.28f);
    snare_ring.thickness = 0.025f;
    snare_ring.color.set(1.0f, 1.0f, 0.2f, 1.0f);

    // Hat ring
    auto& hat_ring = chain.add<Shape>("hat_ring");
    hat_ring.type = ShapeType::Ring;
    hat_ring.size.set(0.21f, 0.21f);
    hat_ring.thickness = 0.02f;
    hat_ring.color.set(0.6f, 0.8f, 1.0f, 1.0f);

    // Clap ring
    auto& clap_ring = chain.add<Shape>("clap_ring");
    clap_ring.type = ShapeType::Ring;
    clap_ring.size.set(0.14f, 0.14f);
    clap_ring.thickness = 0.015f;
    clap_ring.color.set(0.4f, 1.0f, 0.5f, 1.0f);

    // Composite
    auto& comp1 = chain.add<Composite>("comp1");
    comp1.inputA("bg");
    comp1.inputB("kick_ring");
    comp1.mode = BlendMode::Add;

    auto& comp2 = chain.add<Composite>("comp2");
    comp2.inputA("comp1");
    comp2.inputB("snare_ring");
    comp2.mode = BlendMode::Add;

    auto& comp3 = chain.add<Composite>("comp3");
    comp3.inputA("comp2");
    comp3.inputB("hat_ring");
    comp3.mode = BlendMode::Add;

    auto& comp4 = chain.add<Composite>("comp4");
    comp4.inputA("comp3");
    comp4.inputB("clap_ring");
    comp4.mode = BlendMode::Add;

    // Bloom for glow
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("comp4");
    bloom.threshold = 0.3f;
    bloom.intensity = 2.0f;
    bloom.radius = 30.0f;

    chain.output("bloom");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    auto& clock = chain.get<Clock>("clock");

    // Get drums and euclidean patterns
    auto& kick = chain.get<Kick>("kick");
    auto& snare = chain.get<Snare>("snare");
    auto& hihat = chain.get<HiHat>("hihat");
    auto& clap = chain.get<Clap>("clap");

    auto& kick_eucl = chain.get<Euclidean>("kick_eucl");
    auto& snare_eucl = chain.get<Euclidean>("snare_eucl");
    auto& hat_eucl = chain.get<Euclidean>("hat_eucl");
    auto& clap_eucl = chain.get<Euclidean>("clap_eucl");

    // Mouse controls
    float mouseX = ctx.mouseNorm().x;  // 0-1
    float mouseY = ctx.mouseNorm().y;  // 0-1

    // X: BPM (80-160)
    clock.bpm = 80.0f + mouseX * 80.0f;

    // Y: Pattern complexity (hits)
    int baseHits = 2 + static_cast<int>(mouseY * 6);
    kick_eucl.hits = std::min(baseHits, 8);
    snare_eucl.hits = std::min(baseHits - 1, 7);
    hat_eucl.hits = std::min(baseHits + 2, 12);
    clap_eucl.hits = std::min(baseHits + 1, 10);

    // Process clock
    if (clock.triggered()) {
        // Advance all euclidean patterns
        kick_eucl.advance();
        snare_eucl.advance();
        hat_eucl.advance();
        clap_eucl.advance();

        // Trigger drums
        if (kick_eucl.triggered()) {
            kick.trigger();
        }
        if (snare_eucl.triggered()) {
            snare.trigger();
        }
        if (hat_eucl.triggered()) {
            hihat.trigger();
        }
        if (clap_eucl.triggered()) {
            clap.trigger();
        }
    }

    // Visual feedback - rings pulse with envelopes
    float kickEnv = kick.ampEnvelope();
    float snareEnv = snare.ampEnvelope();
    float hatEnv = hihat.ampEnvelope();
    float clapEnv = clap.ampEnvelope();

    auto& kick_ring = chain.get<Shape>("kick_ring");
    kick_ring.size.set(0.3f + kickEnv * 0.15f, 0.3f + kickEnv * 0.15f);
    kick_ring.thickness = 0.02f + kickEnv * 0.03f;

    auto& snare_ring = chain.get<Shape>("snare_ring");
    snare_ring.size.set(0.24f + snareEnv * 0.12f, 0.24f + snareEnv * 0.12f);
    snare_ring.thickness = 0.018f + snareEnv * 0.02f;

    auto& hat_ring = chain.get<Shape>("hat_ring");
    hat_ring.size.set(0.18f + hatEnv * 0.08f, 0.18f + hatEnv * 0.08f);
    hat_ring.thickness = 0.015f + hatEnv * 0.015f;

    auto& clap_ring = chain.get<Shape>("clap_ring");
    clap_ring.size.set(0.12f + clapEnv * 0.06f, 0.12f + clapEnv * 0.06f);
    clap_ring.thickness = 0.012f + clapEnv * 0.01f;

    // Rotate rings at different speeds for visual interest
    kick_ring.rotation = t * 0.1f;
    snare_ring.rotation = -t * 0.15f;
    hat_ring.rotation = t * 0.2f;
    clap_ring.rotation = -t * 0.25f;
}

VIVID_CHAIN(setup, update)
