// Audio-Reactive Visual Test
//
// Demonstrates audio-reactive parameter modulation
// Static noise, geometric shapes, scanlines
// Monochrome aesthetic with red accent
//
// Note: Uses manual update() modulation rather than Param<T>.bind()
// because bind() lambdas capturing chain references cause issues.

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <vivid/audio_output.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // Audio
    // =========================================================================

    auto& synth = chain.add<PolySynth>("synth");
    synth.waveform(Waveform::Square);
    synth.attack = 0.01f;
    synth.decay = 0.1f;
    synth.sustain = 0.3f;
    synth.release = 0.5f;
    synth.volume = 0.4f;

    auto& bands = chain.add<BandSplit>("bands");
    bands.input("synth");

    auto& levels = chain.add<Levels>("levels");
    levels.input("synth");

    auto& audioOut = chain.add<AudioOutput>("audioOut");
    audioOut.setInput("synth");
    chain.audioOutput("audioOut");

    // =========================================================================
    // Visuals - Layer 1: Static noise background
    // =========================================================================

    auto& staticNoise = chain.add<Noise>("static");
    staticNoise.scale = 200.0f;  // Fine grain
    staticNoise.speed = 50.0f;   // Fast flickering
    staticNoise.octaves = 1;

    // Quantize to harsh black/white
    auto& quantize = chain.add<Quantize>("quantize");
    quantize.input(&staticNoise);
    quantize.levels = 2;

    // =========================================================================
    // Visuals - Layer 2: Bold geometric shapes
    // =========================================================================

    // Center circle - pulses with bass (smaller to leave room for text)
    auto& circle = chain.add<Shape>("circle");
    circle.type(ShapeType::Circle);
    circle.position.set(0.5f, 0.65f);  // Moved down
    circle.size.set(0.15f, 0.15f);     // Smaller
    circle.color.set(1.0f, 0.0f, 0.0f, 1.0f);  // Red
    circle.softness = 0.0f;  // Hard edge

    // Thick horizontal bars
    auto& bar1 = chain.add<Shape>("bar1");
    bar1.type(ShapeType::Rectangle);
    bar1.position.set(0.5f, 0.15f);
    bar1.size.set(0.8f, 0.08f);
    bar1.color.set(1.0f, 1.0f, 1.0f, 1.0f);  // White
    bar1.softness = 0.0f;

    auto& bar2 = chain.add<Shape>("bar2");
    bar2.type(ShapeType::Rectangle);
    bar2.position.set(0.5f, 0.85f);
    bar2.size.set(0.8f, 0.08f);
    bar2.color.set(1.0f, 1.0f, 1.0f, 1.0f);
    bar2.softness = 0.0f;

    // =========================================================================
    // Visuals - Layer 3: Typography
    // =========================================================================

    auto& canvas = chain.add<Canvas>("text");
    canvas.size(1920, 1080);

    // Load font - Geneva at 80px provides larger, bolder text
    canvas.loadFont(ctx, "/System/Library/Fonts/Geneva.ttf", 80.0f);

    // =========================================================================
    // Composite all layers
    // =========================================================================

    // Static + circle
    auto& comp1 = chain.add<Composite>("comp1");
    comp1.inputA(&quantize);
    comp1.inputB(&circle);
    comp1.mode(BlendMode::Add);
    comp1.opacity = 0.8f;

    // Add bars
    auto& comp2 = chain.add<Composite>("comp2");
    comp2.inputA(&comp1);
    comp2.inputB(&bar1);
    comp2.mode(BlendMode::Screen);

    auto& comp3 = chain.add<Composite>("comp3");
    comp3.inputA(&comp2);
    comp3.inputB(&bar2);
    comp3.mode(BlendMode::Screen);

    // Desaturate the geometric layers (not the text)
    auto& hsv = chain.add<HSV>("hsv");
    hsv.input(&comp3);
    hsv.saturation = 0.0f;  // Monochrome base

    // Re-add red through flash
    auto& flash = chain.add<Flash>("flash");
    flash.input(&hsv);
    flash.color.set(0.8f, 0.0f, 0.0f);
    flash.decay = 0.9f;
    flash.mode = 0;  // Additive

    // Add text on top (after HSV so it stays white)
    auto& comp4 = chain.add<Composite>("comp4");
    comp4.inputA(&flash);   // Flash result as base
    comp4.inputB(&canvas);  // Canvas (text) blended on top
    comp4.mode(BlendMode::Over);

    // Final scanlines
    auto& scanlines = chain.add<Scanlines>("scanlines");
    scanlines.input(&comp4);
    scanlines.spacing = 3;
    scanlines.intensity = 0.15f;

    chain.output("scanlines");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = ctx.time();

    // Update all parameters based on audio analysis
    auto& bands = chain.get<BandSplit>("bands");
    auto& levels = chain.get<Levels>("levels");

    float bass = bands.bass();
    float mid = bands.mid();
    float high = bands.high();
    float rms = levels.rms();

    // Circle size pulses with bass
    float circleSize = 0.1f + bass * 0.15f;
    chain.get<Shape>("circle").size.set(circleSize, circleSize);

    // Bar width with mid
    float barWidth = 0.4f + mid * 0.6f;
    chain.get<Shape>("bar1").size.set(barWidth, 0.08f);
    chain.get<Shape>("bar2").size.set(barWidth, 0.08f);

    // Static intensity with high frequencies
    chain.get<Noise>("static").scale = 100.0f + high * 300.0f;

    // Flash decay on loud moments
    chain.get<Flash>("flash").decay = 0.85f + rms * 0.13f;

    // Scanline intensity
    chain.get<Scanlines>("scanlines").intensity = 0.1f + bass * 0.3f;

    // Draw bold text
    auto& canvas = chain.get<Canvas>("text");
    canvas.clear(0, 0, 0, 0);  // Transparent background

    // Pulsing text (using rms from levels already retrieved above)
    float pulse = 0.7f + rms * 0.5f;

    canvas.fillStyle(1.0f, 1.0f, 1.0f, pulse);
    canvas.textAlign(TextAlign::Center);
    canvas.textBaseline(TextBaseline::Middle);
    canvas.fillText("VIVID", 960, 540);

    auto& synth = chain.get<PolySynth>("synth");
    auto& flash = chain.get<Flash>("flash");

    // Auto-trigger on beat (every ~0.5 seconds)
    static float lastTrigger = 0.0f;
    float beatInterval = 0.5f;

    if (time - lastTrigger > beatInterval) {
        lastTrigger = time;
        synth.allNotesOff();

        // Cycle through bass notes
        float notes[] = {55.0f, 65.41f, 73.42f, 82.41f, 55.0f, 82.41f, 73.42f, 65.41f};
        int idx = static_cast<int>(time / beatInterval) % 8;
        synth.noteOn(notes[idx]);
        flash.trigger();
    }

    // Click for manual trigger
    if (ctx.mouseButton(0).pressed) {
        flash.trigger(1.0f);
    }

    // Space to silence
    if (ctx.key(GLFW_KEY_SPACE).pressed) {
        synth.allNotesOff();
    }

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
