// Audio Effects - Vivid Example
// Demonstrates: Compressor, Phaser, Flanger, Bitcrush, Overdrive
//
// Audio effect chain with selectable effects

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

// Track which effect is active
static int g_activeEffect = 0;
static const int NUM_EFFECTS = 5;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // ----- AUDIO SOURCE -----
    // Simple synth as effect input
    auto& synth = chain.add<Synth>("synth");
    synth.waveform(Waveform::Sawtooth);
    synth.attack = 0.01f;
    synth.decay = 0.2f;
    synth.sustain = 0.6f;
    synth.release = 0.3f;
    synth.volume = 0.7f;

    // Clock for auto-triggering
    auto& clock = chain.add<Clock>("clock");
    clock.bpm = 80.0f;
    clock.division(ClockDiv::Quarter);
    clock.start();

    // ----- AUDIO EFFECTS -----
    // Each effect processes the synth output

    // 1. COMPRESSOR - Dynamic range control
    auto& comp = chain.add<Compressor>("compressor");
    comp.input("synth");
    comp.threshold = -18.0f;   // Compress above -18dB
    comp.ratio = 6.0f;         // 6:1 compression
    comp.attack = 5.0f;        // 5ms attack
    comp.release = 80.0f;      // 80ms release
    comp.makeupGain = 6.0f;    // +6dB makeup
    comp.knee = 3.0f;          // Soft knee
    comp.mix = 1.0f;

    // 2. PHASER - Sweeping notches
    auto& phaser = chain.add<Phaser>("phaser");
    phaser.input("synth");
    phaser.rate = 0.4f;        // 0.4 Hz sweep
    phaser.depth = 0.9f;       // Deep modulation
    phaser.stages = 8;         // 8 all-pass stages
    phaser.feedback = 0.6f;    // Resonance
    phaser.mix = 0.5f;

    // 3. FLANGER - Short delay with modulation
    auto& flanger = chain.add<Flanger>("flanger");
    flanger.input("synth");
    flanger.rate = 0.25f;      // Slow sweep
    flanger.depth = 0.8f;      // Deep modulation
    flanger.feedback = 0.7f;   // High feedback for jet sound
    flanger.mix = 0.5f;

    // 4. BITCRUSH - Lo-fi digital destruction
    auto& bitcrush = chain.add<Bitcrush>("bitcrush");
    bitcrush.input("synth");
    bitcrush.bits = 8;         // 8-bit quantization
    bitcrush.sampleRate = 12000.0f;  // Downsample
    bitcrush.mix = 1.0f;

    // 5. OVERDRIVE - Soft clipping saturation
    auto& overdrive = chain.add<Overdrive>("overdrive");
    overdrive.input("synth");
    overdrive.drive = 0.7f;    // Drive amount
    overdrive.tone = 0.6f;     // Tone shaping
    overdrive.mix = 0.8f;

    // ----- MIXER -----
    // We'll switch between effects in update()
    auto& mixer = chain.add<AudioMixer>("mixer");
    mixer.addInput(&comp, 0.0f);      // Start with all off
    mixer.addInput(&phaser, 0.0f);
    mixer.addInput(&flanger, 0.0f);
    mixer.addInput(&bitcrush, 0.0f);
    mixer.addInput(&overdrive, 0.0f);

    // ----- AUDIO OUTPUT -----
    auto& output = chain.add<AudioOutput>("audio_out");
    output.input("mixer");

    // ----- VISUALS -----
    // Effect visualization
    auto& bg = chain.add<Gradient>("bg");
    bg.mode(GradientMode::Radial);
    bg.colorA.set(0.15f, 0.1f, 0.2f, 1.0f);
    bg.colorB.set(0.02f, 0.02f, 0.04f, 1.0f);

    // Waveform-inspired shape
    auto& shape = chain.add<Shape>("shape");
    shape.type(ShapeType::Circle);
    shape.size.set(0.25f, 0.25f);
    shape.softness = 0.3f;
    shape.color.set(0.5f, 0.8f, 1.0f, 1.0f);

    auto& comp1 = chain.add<Composite>("comp1");
    comp1.inputA("bg");
    comp1.inputB("shape");
    comp1.mode(BlendMode::Add);

    // Post-process with bloom
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("comp1");
    bloom.threshold = 0.4f;
    bloom.intensity = 1.5f;
    bloom.radius = 30.0f;

    chain.output("bloom");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    auto& synth = chain.get<Synth>("synth");
    auto& clock = chain.get<Clock>("clock");
    auto& mixer = chain.get<AudioMixer>("mixer");

    // Mouse X: select effect (0-4)
    float mouseX = ctx.mouseNorm().x * 0.5f + 0.5f;  // 0-1
    g_activeEffect = static_cast<int>(mouseX * NUM_EFFECTS);
    if (g_activeEffect >= NUM_EFFECTS) g_activeEffect = NUM_EFFECTS - 1;

    // Set mixer levels based on active effect
    for (int i = 0; i < NUM_EFFECTS; i++) {
        mixer.setLevel(i, (i == g_activeEffect) ? 1.0f : 0.0f);
    }

    // Mouse Y: effect intensity parameter
    float mouseY = ctx.mouseNorm().y * 0.5f + 0.5f;  // 0-1

    // Adjust active effect parameter
    switch (g_activeEffect) {
        case 0: {  // Compressor
            auto& comp = chain.get<Compressor>("compressor");
            comp.ratio = 2.0f + mouseY * 18.0f;  // 2:1 to 20:1
            break;
        }
        case 1: {  // Phaser
            auto& phaser = chain.get<Phaser>("phaser");
            phaser.rate = 0.1f + mouseY * 2.0f;  // 0.1 to 2.1 Hz
            phaser.feedback = mouseY * 0.9f;
            break;
        }
        case 2: {  // Flanger
            auto& flanger = chain.get<Flanger>("flanger");
            flanger.rate = 0.05f + mouseY * 1.0f;  // 0.05 to 1.05 Hz
            flanger.feedback = mouseY * 0.95f;
            break;
        }
        case 3: {  // Bitcrush
            auto& bitcrush = chain.get<Bitcrush>("bitcrush");
            bitcrush.bits = 2 + static_cast<int>((1.0f - mouseY) * 14);  // 2-16 bits
            bitcrush.sampleRate = 4000.0f + (1.0f - mouseY) * 40000.0f;
            break;
        }
        case 4: {  // Overdrive
            auto& overdrive = chain.get<Overdrive>("overdrive");
            overdrive.drive = mouseY;  // 0-1
            break;
        }
    }

    // Auto-trigger synth with clock
    if (clock.triggered()) {
        // Cycle through notes in a simple pattern
        static const int notes[] = {48, 51, 55, 48, 53, 55, 51, 48};
        static int noteIndex = 0;
        synth.note(notes[noteIndex]);
        synth.trigger();
        noteIndex = (noteIndex + 1) % 8;
    }

    // Visual feedback
    auto& shape = chain.get<Shape>("shape");

    // Color based on active effect
    float hue = static_cast<float>(g_activeEffect) / NUM_EFFECTS;
    shape.color.set(
        0.5f + 0.5f * std::sin(hue * 6.28f),
        0.5f + 0.5f * std::sin(hue * 6.28f + 2.09f),
        0.5f + 0.5f * std::sin(hue * 6.28f + 4.19f),
        1.0f
    );

    // Pulse with synth envelope
    float envScale = synth.isActive() ? 0.1f : 0.0f;
    float size = 0.2f + envScale + 0.05f * std::sin(t * 3.0f);
    shape.size.set(size, size);

    // Effect-specific animation
    switch (g_activeEffect) {
        case 1:  // Phaser - swirl
            shape.rotation = t * 0.5f;
            break;
        case 2:  // Flanger - jet sweep
            shape.rotation = std::sin(t * 0.3f) * 0.5f;
            break;
        case 3:  // Bitcrush - jittery
            shape.position.set(
                std::sin(t * 20.0f) * 0.01f,
                std::cos(t * 23.0f) * 0.01f
            );
            break;
        case 4:  // Overdrive - throb
            {
                float throb = 0.2f + mouseY * 0.15f;
                shape.size.set(size + throb * std::sin(t * 8.0f) * 0.1f,
                               size + throb * std::sin(t * 8.0f) * 0.1f);
            }
            break;
        default:
            shape.rotation = 0.0f;
            shape.position.set(0.0f, 0.0f);
            break;
    }

    // Background color shift
    auto& bg = chain.get<Gradient>("bg");
    bg.colorA.set(
        0.1f + 0.1f * std::sin(hue * 6.28f + t * 0.2f),
        0.08f + 0.08f * std::sin(hue * 6.28f + 2.09f + t * 0.2f),
        0.15f + 0.1f * std::sin(hue * 6.28f + 4.19f + t * 0.2f),
        1.0f
    );
}

VIVID_CHAIN(setup, update)
