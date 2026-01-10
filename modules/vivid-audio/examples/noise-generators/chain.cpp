// Noise Generators Example
// Demonstrates: NoiseGen, Crackle, AudioFilter, Decay
//
// Shows different noise colors and impulse textures for synthesis

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // ----- NOISE GENERATORS -----
    // Three noise colors: white (bright), pink (balanced), brown (deep)

    auto& white = chain.add<NoiseGen>("white");
    white.setColor(NoiseColor::White);
    white.volume = 0.0f;  // Start silent, controlled by UI

    auto& pink = chain.add<NoiseGen>("pink");
    pink.setColor(NoiseColor::Pink);
    pink.volume = 0.0f;

    auto& brown = chain.add<NoiseGen>("brown");
    brown.setColor(NoiseColor::Brown);
    brown.volume = 0.0f;

    // ----- FILTERED NOISE (for percussion) -----
    // White noise through highpass = hi-hat texture

    auto& hihat_noise = chain.add<NoiseGen>("hihat_noise");
    hihat_noise.setColor(NoiseColor::White);
    hihat_noise.volume = 1.0f;

    auto& hihat_filter = chain.add<AudioFilter>("hihat_filter");
    hihat_filter.setInput(&hihat_noise);
    hihat_filter.setHighpass(8000.0f);
    hihat_filter.resonance = 1.5f;

    auto& hihat_env = chain.add<Decay>("hihat_env");
    hihat_env.setInput(&hihat_filter);
    hihat_env.time = 0.05f;
    hihat_env.setCurve(DecayCurve::Exponential);

    // ----- CRACKLE TEXTURE -----
    // Vinyl-style random impulses

    auto& crackle = chain.add<Crackle>("crackle");
    crackle.density = 0.0008f;  // Sparse clicks
    crackle.volume = 0.15f;

    // ----- CLOCK FOR RHYTHM -----
    auto& clock = chain.add<Clock>("clock");
    clock.bpm = 120.0f;
    clock.division(ClockDiv::Eighth);
    clock.start();

    // ----- AUDIO MIXER -----
    auto& mixer = chain.add<AudioMixer>("mixer");
    mixer.addInput(&white, 1.0f);
    mixer.addInput(&pink, 1.0f);
    mixer.addInput(&brown, 1.0f);
    mixer.addInput(&hihat_env, 0.4f);
    mixer.addInput(&crackle, 1.0f);

    // ----- AUDIO OUTPUT -----
    auto& output = chain.add<AudioOutput>("audio_out");
    output.input("mixer");

    // ----- VISUALS -----
    // Background
    auto& bg = chain.add<SolidColor>("bg");
    bg.color.set(0.02f, 0.02f, 0.04f, 1.0f);

    // Noise visualization bars
    auto& white_bar = chain.add<Shape>("white_bar");
    white_bar.type = ShapeType::Rectangle;
    white_bar.position.set(-0.5f, 0.3f);
    white_bar.size.set(0.15f, 0.02f);
    white_bar.color.set(1.0f, 1.0f, 1.0f, 0.8f);

    auto& pink_bar = chain.add<Shape>("pink_bar");
    pink_bar.type = ShapeType::Rectangle;
    pink_bar.position.set(0.0f, 0.3f);
    pink_bar.size.set(0.15f, 0.02f);
    pink_bar.color.set(1.0f, 0.4f, 0.6f, 0.8f);

    auto& brown_bar = chain.add<Shape>("brown_bar");
    brown_bar.type = ShapeType::Rectangle;
    brown_bar.position.set(0.5f, 0.3f);
    brown_bar.size.set(0.15f, 0.02f);
    brown_bar.color.set(0.6f, 0.3f, 0.1f, 0.8f);

    // Crackle visualization
    auto& crackle_dot = chain.add<Shape>("crackle_dot");
    crackle_dot.type = ShapeType::Ellipse;
    crackle_dot.position.set(0.0f, -0.3f);
    crackle_dot.size.set(0.02f, 0.02f);
    crackle_dot.color.set(0.8f, 0.8f, 0.6f, 1.0f);
    crackle_dot.softness = 0.5f;

    // Composite layers
    auto& comp1 = chain.add<Composite>("comp1");
    comp1.inputA("bg");
    comp1.inputB("white_bar");
    comp1.mode = BlendMode::Add;

    auto& comp2 = chain.add<Composite>("comp2");
    comp2.inputA("comp1");
    comp2.inputB("pink_bar");
    comp2.mode = BlendMode::Add;

    auto& comp3 = chain.add<Composite>("comp3");
    comp3.inputA("comp2");
    comp3.inputB("brown_bar");
    comp3.mode = BlendMode::Add;

    auto& comp4 = chain.add<Composite>("comp4");
    comp4.inputA("comp3");
    comp4.inputB("crackle_dot");
    comp4.mode = BlendMode::Add;

    chain.output("comp4");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    // Get operators
    auto& white = chain.get<NoiseGen>("white");
    auto& pink = chain.get<NoiseGen>("pink");
    auto& brown = chain.get<NoiseGen>("brown");
    auto& crackle = chain.get<Crackle>("crackle");
    auto& hihat_env = chain.get<Decay>("hihat_env");
    auto& hihat_filter = chain.get<AudioFilter>("hihat_filter");
    auto& clock = chain.get<Clock>("clock");

    // ----- MOUSE CONTROL -----
    float mouseX = ctx.mouseNorm().x;  // 0-1
    float mouseY = ctx.mouseNorm().y;  // 0-1

    // X position selects noise color (left=white, center=pink, right=brown)
    // Y position controls volume
    float vol = (1.0f - mouseY) * 0.4f;

    if (mouseX < 0.33f) {
        white.volume = vol;
        pink.volume = 0.0f;
        brown.volume = 0.0f;
    } else if (mouseX < 0.66f) {
        white.volume = 0.0f;
        pink.volume = vol;
        brown.volume = 0.0f;
    } else {
        white.volume = 0.0f;
        pink.volume = 0.0f;
        brown.volume = vol;
    }

    // ----- HIHAT TRIGGER -----
    if (clock.triggered()) {
        hihat_env.trigger();
    }

    // Animate filter cutoff
    hihat_filter.cutoff = 6000.0f + 4000.0f * std::sin(t * 0.5f);

    // ----- CRACKLE DENSITY -----
    // Modulate crackle density slowly
    crackle.density = 0.0005f + 0.0005f * std::sin(t * 0.2f);

    // ----- VISUAL FEEDBACK -----
    // Update bar sizes based on volume
    auto& white_bar = chain.get<Shape>("white_bar");
    auto& pink_bar = chain.get<Shape>("pink_bar");
    auto& brown_bar = chain.get<Shape>("brown_bar");

    float whiteVol = static_cast<float>(white.volume);
    float pinkVol = static_cast<float>(pink.volume);
    float brownVol = static_cast<float>(brown.volume);

    white_bar.size.set(0.15f, 0.02f + whiteVol * 0.3f);
    pink_bar.size.set(0.15f, 0.02f + pinkVol * 0.3f);
    brown_bar.size.set(0.15f, 0.02f + brownVol * 0.3f);

    // Crackle dot pulses randomly
    auto& crackle_dot = chain.get<Shape>("crackle_dot");
    float crackleSize = 0.02f + 0.08f * std::pow(std::sin(t * 47.0f), 32.0f);
    crackle_dot.size.set(crackleSize, crackleSize);
}

VIVID_CHAIN(setup, update)
