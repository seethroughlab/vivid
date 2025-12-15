// Audio Visualizer - Vivid Showcase
// Stunning audio-reactive visuals with FFT analysis and beat detection
//
// Controls:
//   M: Toggle Microphone/Generated audio
//   SPACE: Start/Stop audio
//   1-3: Visual preset
//   UP/DOWN: Sensitivity
//   TAB: Parameter controls

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <vivid/audio_output.h>
#include <iostream>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

// State
static int visualPreset = 0;
static bool useMic = false;
static float sensitivity = 1.5f;
static float beatFlash = 0.0f;
static float bassAccum = 0.0f;

void printStatus() {
    std::cout << "\r[" << (useMic ? "MIC" : "SYNTH") << "] "
              << "Preset: " << (visualPreset + 1) << " | "
              << "Sensitivity: " << sensitivity << "   " << std::flush;
}

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // Audio Sources
    // =========================================================================

    // Internal synth - generates beats for visualization
    auto& clock = chain.add<Clock>("clock");
    clock.bpm = 128.0f;
    clock.division(ClockDiv::Sixteenth);

    auto& kickSeq = chain.add<Sequencer>("kickSeq");
    kickSeq.setPattern(0x1111);  // Four on floor

    auto& snareSeq = chain.add<Sequencer>("snareSeq");
    snareSeq.setPattern(0x0404);  // Backbeat

    auto& hihatSeq = chain.add<Sequencer>("hihatSeq");
    hihatSeq.setPattern(0x5555);  // 8th notes

    auto& kick = chain.add<Kick>("kick");
    kick.pitch = 45.0f;
    kick.pitchEnv = 150.0f;
    kick.decay = 0.35f;
    kick.drive = 0.3f;
    kick.volume = 0.9f;

    auto& snare = chain.add<Snare>("snare");
    snare.tone = 0.5f;
    snare.noise = 0.6f;
    snare.toneDecay = 0.1f;
    snare.noiseDecay = 0.2f;
    snare.volume = 0.6f;

    auto& hihat = chain.add<HiHat>("hihat");
    hihat.decay = 0.04f;
    hihat.tone = 0.8f;
    hihat.volume = 0.3f;

    auto& mixer = chain.add<AudioMixer>("mixer");
    mixer.setInput(0, "kick");
    mixer.setGain(0, 1.0f);
    mixer.setInput(1, "snare");
    mixer.setGain(1, 0.7f);
    mixer.setInput(2, "hihat");
    mixer.setGain(2, 0.4f);
    mixer.volume = 0.8f;

    // Microphone input
    auto& mic = chain.add<AudioIn>("mic");
    mic.volume = 1.5f;
    mic.setMute(true);

    // Audio output
    auto& audioOut = chain.add<AudioOutput>("audioOut");
    audioOut.setInput("mixer");
    audioOut.setVolume(0.7f);
    chain.audioOutput("audioOut");

    // =========================================================================
    // Audio Analysis
    // =========================================================================

    auto& fft = chain.add<FFT>("fft");
    fft.input("mixer");
    fft.setSize(512);
    fft.smoothing = 0.75f;

    auto& bands = chain.add<BandSplit>("bands");
    bands.input("mixer");
    bands.smoothing = 0.85f;

    auto& beat = chain.add<BeatDetect>("beat");
    beat.input("mixer");
    beat.sensitivity = sensitivity;
    beat.decay = 0.9f;
    beat.holdTime = 80.0f;

    auto& levels = chain.add<Levels>("levels");
    levels.input("mixer");
    levels.smoothing = 0.8f;

    // =========================================================================
    // Visual Layers
    // =========================================================================

    // Background - dark with subtle color shift
    auto& bg = chain.add<SolidColor>("bg");
    bg.color.set(Color::fromHex("#05050A"));

    // Bass particles - large, slow, react to sub-bass
    auto& bassParticles = chain.add<Particles>("bassParticles");
    bassParticles.emitter(EmitterShape::Ring);
    bassParticles.position(0.5f, 0.5f);
    bassParticles.emitterSize(0.3f);
    bassParticles.emitRate(30.0f);
    bassParticles.maxParticles(3000);
    bassParticles.radialVelocity(0.08f);
    bassParticles.turbulence(0.1f);
    bassParticles.drag(0.8f);
    bassParticles.life(3.0f);
    bassParticles.size(0.025f, 0.005f);
    bassParticles.color(Color::fromHex("#CC3366"));
    bassParticles.colorEnd(Color::fromHex("#661A99").withAlpha(0.0f));
    bassParticles.fadeOut(true);
    bassParticles.clearColor(0.0f, 0.0f, 0.0f, 0.0f);

    // Mid particles - medium, react to mids
    auto& midParticles = chain.add<Particles>("midParticles");
    midParticles.emitter(EmitterShape::Disc);
    midParticles.position(0.5f, 0.5f);
    midParticles.emitterSize(0.2f);
    midParticles.emitRate(60.0f);
    midParticles.maxParticles(4000);
    midParticles.velocity(0.0f, -0.05f);
    midParticles.spread(180.0f);
    midParticles.turbulence(0.15f);
    midParticles.drag(0.5f);
    midParticles.life(2.5f);
    midParticles.size(0.012f, 0.003f);
    midParticles.color(Color::DodgerBlue);
    midParticles.colorEnd(Color::MediumBlue.withAlpha(0.0f));
    midParticles.fadeOut(true);
    midParticles.clearColor(0.0f, 0.0f, 0.0f, 0.0f);

    // High particles - small, fast, sparkle effect
    auto& highParticles = chain.add<Particles>("highParticles");
    highParticles.emitter(EmitterShape::Disc);
    highParticles.position(0.5f, 0.5f);
    highParticles.emitterSize(0.4f);
    highParticles.emitRate(100.0f);
    highParticles.maxParticles(5000);
    highParticles.velocity(0.0f, 0.0f);
    highParticles.radialVelocity(0.15f);
    highParticles.turbulence(0.2f);
    highParticles.drag(0.3f);
    highParticles.life(1.5f);
    highParticles.size(0.006f, 0.001f);
    highParticles.colorMode(ColorMode::Rainbow);
    highParticles.fadeOut(true);
    highParticles.clearColor(0.0f, 0.0f, 0.0f, 0.0f);

    // Composite particles
    auto& particleComp = chain.add<Composite>("particleComp");
    particleComp.input(0, &bg);
    particleComp.input(1, &bassParticles);
    particleComp.input(2, &midParticles);
    particleComp.input(3, &highParticles);
    particleComp.mode(BlendMode::Add);

    // Central shape - pulses with beat
    auto& beatShape = chain.add<Shape>("beatShape");
    beatShape.type(ShapeType::Circle);
    beatShape.position.set(0.5f, 0.5f);
    beatShape.size.set(0.15f, 0.15f);
    beatShape.color.set(Color::White.withAlpha(0.8f));
    beatShape.softness = 0.3f;

    auto& shapeComp = chain.add<Composite>("shapeComp");
    shapeComp.inputA(&particleComp);
    shapeComp.inputB(&beatShape);
    shapeComp.mode(BlendMode::Add);

    // Feedback for trails
    auto& feedback = chain.add<Feedback>("feedback");
    feedback.input(&shapeComp);
    feedback.decay = 0.92f;
    feedback.mix = 0.5f;
    feedback.zoom = 1.002f;
    feedback.rotate = 0.0f;

    // Bloom for glow
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input(&feedback);
    bloom.threshold = 0.15f;
    bloom.intensity = 0.7f;
    bloom.radius = 0.02f;

    // Chromatic aberration - triggered on beat
    auto& chroma = chain.add<ChromaticAberration>("chroma");
    chroma.input(&bloom);
    chroma.amount = 0.0f;
    chroma.radial = true;

    chain.output("chroma");

    // =========================================================================
    // Console Output
    // =========================================================================

    std::cout << "\n========================================" << std::endl;
    std::cout << "Audio Visualizer - Showcase" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  M: Toggle Mic/Synth" << std::endl;
    std::cout << "  SPACE: Start/Stop" << std::endl;
    std::cout << "  1-3: Visual presets" << std::endl;
    std::cout << "  UP/DOWN: Sensitivity" << std::endl;
    std::cout << "  TAB: Parameter controls" << std::endl;
    std::cout << "========================================\n" << std::endl;

    printStatus();
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float dt = ctx.dt();
    float time = static_cast<float>(ctx.time());

    // Get operators
    auto& clock = chain.get<Clock>("clock");
    auto& kickSeq = chain.get<Sequencer>("kickSeq");
    auto& snareSeq = chain.get<Sequencer>("snareSeq");
    auto& hihatSeq = chain.get<Sequencer>("hihatSeq");
    auto& kick = chain.get<Kick>("kick");
    auto& snare = chain.get<Snare>("snare");
    auto& hihat = chain.get<HiHat>("hihat");
    auto& mic = chain.get<AudioIn>("mic");

    auto& fft = chain.get<FFT>("fft");
    auto& bands = chain.get<BandSplit>("bands");
    auto& beat = chain.get<BeatDetect>("beat");
    auto& levels = chain.get<Levels>("levels");

    auto& bassParticles = chain.get<Particles>("bassParticles");
    auto& midParticles = chain.get<Particles>("midParticles");
    auto& highParticles = chain.get<Particles>("highParticles");
    auto& beatShape = chain.get<Shape>("beatShape");
    auto& feedback = chain.get<Feedback>("feedback");
    auto& bloom = chain.get<Bloom>("bloom");
    auto& chroma = chain.get<ChromaticAberration>("chroma");

    // =========================================================================
    // Input Handling
    // =========================================================================

    // Toggle mic (M)
    if (ctx.key(GLFW_KEY_M).pressed) {
        useMic = !useMic;
        if (useMic) {
            mic.setMute(false);
            clock.stop();
            chain.get<FFT>("fft").input("mic");
            chain.get<BandSplit>("bands").input("mic");
            chain.get<BeatDetect>("beat").input("mic");
            chain.get<Levels>("levels").input("mic");
            chain.get<AudioOutput>("audioOut").setInput("mic");
        } else {
            mic.setMute(true);
            clock.start();
            chain.get<FFT>("fft").input("mixer");
            chain.get<BandSplit>("bands").input("mixer");
            chain.get<BeatDetect>("beat").input("mixer");
            chain.get<Levels>("levels").input("mixer");
            chain.get<AudioOutput>("audioOut").setInput("mixer");
        }
        printStatus();
    }

    // Start/stop (SPACE)
    if (ctx.key(GLFW_KEY_SPACE).pressed && !useMic) {
        if (clock.isRunning()) {
            clock.stop();
        } else {
            clock.start();
        }
    }

    // Visual presets (1-3)
    if (ctx.key(GLFW_KEY_1).pressed) {
        visualPreset = 0;
        printStatus();
    }
    if (ctx.key(GLFW_KEY_2).pressed) {
        visualPreset = 1;
        printStatus();
    }
    if (ctx.key(GLFW_KEY_3).pressed) {
        visualPreset = 2;
        printStatus();
    }

    // Sensitivity (UP/DOWN)
    if (ctx.key(GLFW_KEY_UP).pressed) {
        sensitivity = std::min(3.0f, sensitivity + 0.1f);
        beat.sensitivity = sensitivity;
        printStatus();
    }
    if (ctx.key(GLFW_KEY_DOWN).pressed) {
        sensitivity = std::max(0.5f, sensitivity - 0.1f);
        beat.sensitivity = sensitivity;
        printStatus();
    }

    // =========================================================================
    // Sequencer Logic (when using synth)
    // =========================================================================

    if (!useMic && clock.triggered()) {
        kickSeq.advance();
        snareSeq.advance();
        hihatSeq.advance();

        if (kickSeq.triggered()) kick.trigger();
        if (snareSeq.triggered()) snare.trigger();
        if (hihatSeq.triggered()) hihat.trigger();
    }

    // =========================================================================
    // Audio Analysis
    // =========================================================================

    float bass = bands.bass();
    float subBass = bands.subBass();
    float mid = bands.mid();
    float high = bands.high();
    float highMid = bands.highMid();
    float rms = levels.rms();
    bool isBeat = beat.beat();
    float beatIntensity = beat.intensity();
    float energy = beat.energy();

    // Accumulate bass for smoother response
    bassAccum = bassAccum * 0.9f + bass * 0.1f;

    // Beat flash decay
    if (isBeat) {
        beatFlash = 1.0f;
    }
    beatFlash *= 0.85f;

    // =========================================================================
    // Audio-Reactive Visuals
    // =========================================================================

    // Bass particles - emit more on bass, expand ring
    float bassEmit = 20.0f + bass * 200.0f;
    bassParticles.emitRate(bassEmit);
    bassParticles.emitterSize(0.2f + bassAccum * 0.4f);
    bassParticles.radialVelocity(0.05f + subBass * 0.2f);

    // Burst on beat
    if (isBeat) {
        bassParticles.burst(static_cast<int>(50 + beatIntensity * 100));
    }

    // Mid particles - turbulence and emit rate
    float midEmit = 40.0f + mid * 150.0f;
    midParticles.emitRate(midEmit);
    midParticles.turbulence(0.1f + mid * 0.3f);

    // High particles - sparkle intensity
    float highEmit = 60.0f + high * 200.0f + highMid * 100.0f;
    highParticles.emitRate(highEmit);
    highParticles.radialVelocity(0.1f + high * 0.2f);

    // Beat shape - size and color
    float shapeSize = 0.08f + energy * 0.15f + beatFlash * 0.2f;
    beatShape.size.set(shapeSize, shapeSize);
    beatShape.softness = 0.2f + beatFlash * 0.3f;

    // Color based on preset
    float hue = std::fmod(time * 0.05f + bass * 0.3f, 1.0f);
    float r, g, b;
    switch (visualPreset) {
        case 0:  // Neon
            r = 0.9f + beatFlash * 0.1f;
            g = 0.3f + mid * 0.5f;
            b = 0.8f + high * 0.2f;
            break;
        case 1:  // Fire
            r = 1.0f;
            g = 0.4f + bass * 0.4f;
            b = 0.1f + beatFlash * 0.3f;
            break;
        case 2:  // Ice
            r = 0.3f + beatFlash * 0.3f;
            g = 0.7f + mid * 0.3f;
            b = 1.0f;
            break;
        default:
            r = g = b = 1.0f;
    }
    beatShape.color.set(r, g, b, 0.6f + beatFlash * 0.4f);

    // Feedback rotation - subtle sway
    float rotation = 0.002f * std::sin(time * 0.5f) + bassAccum * 0.005f;
    feedback.rotate = rotation;
    feedback.decay = 0.9f + energy * 0.08f;

    // Bloom intensity with energy
    bloom.intensity = 0.5f + energy * 0.5f;
    bloom.radius = 0.015f + bass * 0.02f;

    // Chromatic aberration on beat
    float chromaAmount = beatFlash * 0.015f;
    chroma.amount = chromaAmount;
}

VIVID_CHAIN(setup, update)
