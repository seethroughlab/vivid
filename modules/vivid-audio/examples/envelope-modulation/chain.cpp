// Envelope Modulation - Vivid Example
// Demonstrates: Envelope (ADSR), AR, PitchEnv - Synth modulation patterns
//
// Shows how envelopes shape sound over time

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

// Which envelope demo is active
enum class EnvDemo {
    ADSR,      // Classic ADSR envelope
    AR,        // Attack-Release (plucky)
    PitchSweep // Pitch envelope for kicks
};
static EnvDemo g_demo = EnvDemo::ADSR;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // ----- DEMO 1: ADSR ENVELOPE -----
    // Full Attack-Decay-Sustain-Release envelope for sustained sounds
    auto& osc1 = chain.add<Oscillator>("osc1");
    osc1.frequency(220.0f);
    osc1.waveform(Waveform::Sawtooth);

    auto& env1 = chain.add<Envelope>("env1");
    env1.input("osc1");
    env1.attack = 0.1f;    // 100ms attack
    env1.decay = 0.2f;     // 200ms decay
    env1.sustain = 0.6f;   // 60% sustain level
    env1.release = 0.5f;   // 500ms release

    // Filter modulated by envelope
    auto& filter1 = chain.add<Filter>("filter1");
    filter1.input("env1");
    filter1.type(FilterType::LowPass);
    filter1.cutoff = 800.0f;
    filter1.resonance = 0.5f;

    // ----- DEMO 2: AR ENVELOPE -----
    // Simple Attack-Release for plucks and percussion
    auto& osc2 = chain.add<Oscillator>("osc2");
    osc2.frequency(440.0f);
    osc2.waveform(Waveform::Triangle);

    auto& ar = chain.add<AR>("ar");
    ar.input("osc2");
    ar.attack = 0.005f;    // 5ms attack (instant)
    ar.release = 0.3f;     // 300ms release

    // ----- DEMO 3: PITCH ENVELOPE -----
    // Pitch sweep for kick drums and FX
    auto& osc3 = chain.add<Oscillator>("osc3");
    osc3.waveform(Waveform::Sine);

    auto& pitchEnv = chain.add<PitchEnv>("pitchEnv");
    pitchEnv.startFreq = 200.0f;  // Start at 200Hz
    pitchEnv.endFreq = 50.0f;     // End at 50Hz
    pitchEnv.time = 0.15f;        // 150ms sweep

    // AR envelope for amplitude
    auto& kickEnv = chain.add<AR>("kickEnv");
    kickEnv.input("osc3");
    kickEnv.attack = 0.001f;
    kickEnv.release = 0.4f;

    // ----- MIXER -----
    auto& mixer = chain.add<AudioMixer>("mixer");
    mixer.addInput(&filter1, 0.0f);
    mixer.addInput(&ar, 0.0f);
    mixer.addInput(&kickEnv, 0.0f);

    // ----- OUTPUT -----
    auto& output = chain.add<AudioOutput>("audio_out");
    output.input("mixer");

    // ----- VISUALS -----
    // Envelope visualization
    auto& bg = chain.add<Gradient>("bg");
    bg.mode = GradientMode::Linear;
    bg.angle = 1.5708f;  // 90 degrees (vertical)
    bg.colorA.set(0.1f, 0.05f, 0.15f, 1.0f);
    bg.colorB.set(0.02f, 0.02f, 0.05f, 1.0f);

    // Visual representation of envelope
    auto& envShape = chain.add<Shape>("envShape");
    envShape.type = ShapeType::Rectangle;
    envShape.size.set(0.1f, 0.3f);
    envShape.softness = 0.1f;
    envShape.color.set(0.3f, 0.8f, 0.5f, 1.0f);

    // Stage indicator
    auto& stageIndicator = chain.add<Shape>("stageIndicator");
    stageIndicator.type = ShapeType::Circle;
    stageIndicator.size.set(0.05f, 0.05f);
    stageIndicator.position.set(0.0f, -0.35f);
    stageIndicator.softness = 0.3f;

    auto& comp1 = chain.add<Composite>("comp1");
    comp1.inputA("bg");
    comp1.inputB("envShape");
    comp1.mode = BlendMode::Add;

    auto& comp2 = chain.add<Composite>("comp2");
    comp2.inputA("comp1");
    comp2.inputB("stageIndicator");
    comp2.mode = BlendMode::Add;

    chain.output("comp2");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    // Get operators
    auto& env1 = chain.get<Envelope>("env1");
    auto& ar = chain.get<AR>("ar");
    auto& pitchEnv = chain.get<PitchEnv>("pitchEnv");
    auto& kickEnv = chain.get<AR>("kickEnv");
    auto& osc3 = chain.get<Oscillator>("osc3");
    auto& mixer = chain.get<AudioMixer>("mixer");
    auto& filter1 = chain.get<Filter>("filter1");

    // ----- SELECT DEMO WITH KEYS -----
    // Keys 1, 2, 3 select demo
    if (ctx.keyPressed(Key::Key1)) g_demo = EnvDemo::ADSR;
    if (ctx.keyPressed(Key::Key2)) g_demo = EnvDemo::AR;
    if (ctx.keyPressed(Key::Key3)) g_demo = EnvDemo::PitchSweep;

    // Set mixer levels based on active demo
    mixer.setLevel(0, g_demo == EnvDemo::ADSR ? 0.7f : 0.0f);
    mixer.setLevel(1, g_demo == EnvDemo::AR ? 0.7f : 0.0f);
    mixer.setLevel(2, g_demo == EnvDemo::PitchSweep ? 0.7f : 0.0f);

    // ----- TRIGGER ON SPACE / MOUSE -----
    bool shouldTrigger = ctx.keyPressed(Key::Space) || ctx.mousePressed(0);
    bool shouldRelease = ctx.keyReleased(Key::Space) || ctx.mouseReleased(0);

    // ----- MOUSE Y: ENVELOPE PARAMS -----
    float mouseY = ctx.mouseNorm().y;  // 0-1

    // Current envelope value and stage (for visualization)
    float envValue = 0.0f;
    int stage = 0;

    switch (g_demo) {
        case EnvDemo::ADSR: {
            // Mouse Y controls sustain level
            env1.sustain = mouseY;

            // Modulate filter cutoff with envelope
            float envVal = env1.currentValue();
            filter1.cutoff = 400.0f + envVal * 3000.0f;

            if (shouldTrigger) env1.trigger();
            if (shouldRelease) env1.releaseNote();

            envValue = env1.currentValue();
            stage = static_cast<int>(env1.stage());
            break;
        }

        case EnvDemo::AR: {
            // Mouse Y controls release time
            ar.release = 0.05f + mouseY * 2.0f;

            if (shouldTrigger) ar.trigger();

            envValue = ar.currentValue();
            stage = ar.isActive() ? 1 : 0;
            break;
        }

        case EnvDemo::PitchSweep: {
            // Mouse Y controls start frequency
            pitchEnv.startFreq = 100.0f + mouseY * 300.0f;

            // Apply pitch to oscillator
            osc3.frequency(pitchEnv.currentFreq());

            if (shouldTrigger) {
                pitchEnv.trigger();
                kickEnv.trigger();
            }

            envValue = kickEnv.currentValue();
            stage = pitchEnv.isActive() ? 1 : 0;
            break;
        }
    }

    // ----- UPDATE VISUALS -----
    auto& envShape = chain.get<Shape>("envShape");
    auto& stageIndicator = chain.get<Shape>("stageIndicator");
    auto& bg = chain.get<Gradient>("bg");

    // Envelope bar visualization
    float barHeight = 0.05f + envValue * 0.4f;
    envShape.size.set(0.15f, barHeight);
    envShape.position.set(0.0f, barHeight * 0.5f - 0.2f);

    // Color based on demo
    switch (g_demo) {
        case EnvDemo::ADSR:
            envShape.color.set(0.3f, 0.8f, 0.5f, 1.0f);  // Green
            break;
        case EnvDemo::AR:
            envShape.color.set(0.8f, 0.5f, 0.3f, 1.0f);  // Orange
            break;
        case EnvDemo::PitchSweep:
            envShape.color.set(0.5f, 0.3f, 0.8f, 1.0f);  // Purple
            break;
    }

    // Stage indicator color
    switch (stage) {
        case 0:  // Idle
            stageIndicator.color.set(0.2f, 0.2f, 0.2f, 1.0f);
            break;
        case 1:  // Attack
            stageIndicator.color.set(1.0f, 0.3f, 0.3f, 1.0f);
            break;
        case 2:  // Decay
            stageIndicator.color.set(1.0f, 0.8f, 0.3f, 1.0f);
            break;
        case 3:  // Sustain
            stageIndicator.color.set(0.3f, 1.0f, 0.3f, 1.0f);
            break;
        case 4:  // Release
            stageIndicator.color.set(0.3f, 0.3f, 1.0f, 1.0f);
            break;
    }

    // Subtle background animation
    float bgPulse = 0.02f + envValue * 0.05f;
    bg.colorA.set(0.1f + bgPulse, 0.05f, 0.15f + bgPulse, 1.0f);

    // Debug display
    ctx.debug("demo", static_cast<float>(g_demo));
    ctx.debug("envValue", envValue);
    ctx.debug("stage", static_cast<float>(stage));
}

VIVID_CHAIN(setup, update)
