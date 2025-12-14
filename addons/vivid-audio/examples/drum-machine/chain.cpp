// Drum Machine Demo - Vivid Example
// Demonstrates audio synthesis with drum operators and sequencing
// Controls:
//   SPACE: Start/Stop
//   1-4: Trigger individual drums (Kick, Snare, HiHat, Clap)
//   UP/DOWN: Adjust BPM
//   LEFT/RIGHT: Change pattern
//   E: Toggle Euclidean mode
//   TAB: Open parameter controls

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <vivid/audio_output.h>
#include <iostream>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

// Pattern presets
static const uint16_t kickPatterns[] = {
    0x1111,  // Four on the floor: X...X...X...X...
    0x0101,  // Half time: X.......X.......
    0x1151,  // Syncopated: X...X.X.X...X...
    0x1199,  // Breakbeat: X...X..XX...X..X
};

static const uint16_t snarePatterns[] = {
    0x0404,  // Backbeat: ....X.......X...
    0x0808,  // Offbeat: ........X.......
    0x0C0C,  // Double: ....XX......XX..
    0x2424,  // Syncopated: ..X...X...X...X.
};

static const uint16_t hihatPatterns[] = {
    0xFFFF,  // Every 16th: XXXXXXXXXXXXXXXX
    0x5555,  // Every 8th: X.X.X.X.X.X.X.X.
    0xAAAA,  // Offbeat 8th: .X.X.X.X.X.X.X.X
    0xF5F5,  // Variation: XXXX.X.XXXXX.X.X
};

static const uint16_t clapPatterns[] = {
    0x0404,  // With snare: ....X.......X...
    0x0000,  // None
    0x4040,  // Offbeat: .X...........X..
    0x0808,  // Sparse: ........X.......
};

static int currentPattern = 0;
static const int numPatterns = 4;
static bool useEuclidean = false;

void printStatus(float bpm, bool running) {
    std::cout << "\r[" << (running ? "PLAYING" : "STOPPED") << "] "
              << "BPM: " << static_cast<int>(bpm) << " | "
              << "Pattern: " << (currentPattern + 1) << "/" << numPatterns << " | "
              << "Mode: " << (useEuclidean ? "Euclidean" : "Pattern") << "   " << std::flush;
}

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // Clock - Master Timing
    // =========================================================================

    auto& clock = chain.add<Clock>("clock");
    clock.bpm(120.0f).division(ClockDiv::Sixteenth).swing(0.0f);

    // =========================================================================
    // Sequencers - Pattern-based triggering
    // =========================================================================

    auto& kickSeq = chain.add<Sequencer>("kickSeq");
    auto& snareSeq = chain.add<Sequencer>("snareSeq");
    auto& hihatSeq = chain.add<Sequencer>("hihatSeq");
    auto& clapSeq = chain.add<Sequencer>("clapSeq");

    // Load default patterns
    kickSeq.setPattern(kickPatterns[0]);
    snareSeq.setPattern(snarePatterns[0]);
    hihatSeq.setPattern(hihatPatterns[0]);
    clapSeq.setPattern(clapPatterns[0]);

    // =========================================================================
    // Euclidean Sequencers - Algorithmic alternative
    // =========================================================================

    auto& kickEucl = chain.add<Euclidean>("kickEucl");
    auto& snareEucl = chain.add<Euclidean>("snareEucl");
    auto& hihatEucl = chain.add<Euclidean>("hihatEucl");
    auto& clapEucl = chain.add<Euclidean>("clapEucl");

    // Classic Euclidean patterns
    kickEucl.steps(16).hits(4).rotation(0);    // 4 evenly spaced kicks
    snareEucl.steps(16).hits(2).rotation(4);   // 2 snares, offset to backbeat
    hihatEucl.steps(16).hits(8).rotation(0);   // 8th note hi-hats
    clapEucl.steps(16).hits(3).rotation(2);    // Tresillo-ish claps

    // =========================================================================
    // Drum Voices
    // =========================================================================

    // 808-style kick
    auto& kick = chain.add<Kick>("kick");
    kick.pitch(50.0f)
        .pitchEnv(120.0f)
        .pitchDecay(0.08f)
        .decay(0.4f)
        .click(0.4f)
        .drive(0.2f)
        .volume(0.9f);

    // Punchy snare
    auto& snare = chain.add<Snare>("snare");
    snare.tone(0.4f)
         .noise(0.7f)
         .pitch(180.0f)
         .toneDecay(0.08f)
         .noiseDecay(0.15f)
         .snappy(0.6f)
         .volume(0.7f);

    // Closed hi-hat
    auto& hihat = chain.add<HiHat>("hihat");
    hihat.decay(0.05f)
         .tone(0.7f)
         .ring(0.4f)
         .volume(0.4f);

    // Hand clap
    auto& clap = chain.add<Clap>("clap");
    clap.decay(0.25f)
        .tone(0.5f)
        .spread(0.6f)
        .volume(0.5f);

    // =========================================================================
    // Audio Output
    // =========================================================================

    // Mix all drums together
    auto& mixer = chain.add<AudioMixer>("mixer");
    mixer.input(0, "kick").gain(0, 1.0f)
         .input(1, "snare").gain(1, 0.8f)
         .input(2, "hihat").gain(2, 0.5f)
         .input(3, "clap").gain(3, 0.6f)
         .volume(0.8f);

    auto& audioOut = chain.add<AudioOutput>("audioOut");
    audioOut.input("mixer").volume(1.0f);
    chain.audioOutput("audioOut");

    // =========================================================================
    // Visual Feedback - Drum hit visualization
    // =========================================================================

    // Background
    auto& bg = chain.add<SolidColor>("bg");
    bg.color(Color::fromHex("#0D0D14"));

    // Kick visualizer (bottom) - red/orange
    auto& kickVis = chain.add<Shape>("kickVis");
    kickVis.type(ShapeType::Circle)
           .position(0.5f, 0.3f)
           .size(0.15f)
           .color(Color::Tomato)
           .softness(0.1f);

    // Snare visualizer (center-left) - yellow/gold
    auto& snareVis = chain.add<Shape>("snareVis");
    snareVis.type(ShapeType::Circle)
            .position(0.35f, 0.5f)
            .size(0.12f)
            .color(Color::Gold)
            .softness(0.1f);

    // Hi-hat visualizer (center-right) - cyan
    auto& hihatVis = chain.add<Shape>("hihatVis");
    hihatVis.type(ShapeType::Circle)
            .position(0.65f, 0.5f)
            .size(0.08f)
            .color(Color::Cyan)
            .softness(0.1f);

    // Clap visualizer (top) - magenta/violet
    auto& clapVis = chain.add<Shape>("clapVis");
    clapVis.type(ShapeType::Circle)
           .position(0.5f, 0.7f)
           .size(0.1f)
           .color(Color::Orchid)
           .softness(0.1f);

    // Composite all layers
    auto& comp = chain.add<Composite>("comp");
    comp.input(0, &bg)
        .input(1, &kickVis)
        .input(2, &snareVis)
        .input(3, &hihatVis)
        .input(4, &clapVis)
        .mode(BlendMode::Add);

    chain.output("comp");

    // =========================================================================
    // Console Output
    // =========================================================================

    std::cout << "\n========================================" << std::endl;
    std::cout << "Drum Machine Demo" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  SPACE: Start/Stop" << std::endl;
    std::cout << "  1-4: Trigger drums (K/S/H/C)" << std::endl;
    std::cout << "  UP/DOWN: Adjust BPM (+/-5)" << std::endl;
    std::cout << "  LEFT/RIGHT: Change pattern" << std::endl;
    std::cout << "  E: Toggle Euclidean mode" << std::endl;
    std::cout << "  S: Adjust swing" << std::endl;
    std::cout << "  TAB: Open parameter controls" << std::endl;
    std::cout << "========================================\n" << std::endl;

    printStatus(120.0f, true);
}

// Visual decay values
static float kickDecay = 0.0f;
static float snareDecay = 0.0f;
static float hihatDecay = 0.0f;
static float clapDecay = 0.0f;

void update(Context& ctx) {
    auto& chain = ctx.chain();

    // Get operators
    auto& clock = chain.get<Clock>("clock");
    auto& kickSeq = chain.get<Sequencer>("kickSeq");
    auto& snareSeq = chain.get<Sequencer>("snareSeq");
    auto& hihatSeq = chain.get<Sequencer>("hihatSeq");
    auto& clapSeq = chain.get<Sequencer>("clapSeq");

    auto& kickEucl = chain.get<Euclidean>("kickEucl");
    auto& snareEucl = chain.get<Euclidean>("snareEucl");
    auto& hihatEucl = chain.get<Euclidean>("hihatEucl");
    auto& clapEucl = chain.get<Euclidean>("clapEucl");

    auto& kick = chain.get<Kick>("kick");
    auto& snare = chain.get<Snare>("snare");
    auto& hihat = chain.get<HiHat>("hihat");
    auto& clap = chain.get<Clap>("clap");

    auto& kickVis = chain.get<Shape>("kickVis");
    auto& snareVis = chain.get<Shape>("snareVis");
    auto& hihatVis = chain.get<Shape>("hihatVis");
    auto& clapVis = chain.get<Shape>("clapVis");

    // =========================================================================
    // Input Controls
    // =========================================================================

    // Space - start/stop
    if (ctx.key(GLFW_KEY_SPACE).pressed) {
        if (clock.isRunning()) {
            clock.stop();
        } else {
            clock.start();
        }
        printStatus(clock.getBpm(), clock.isRunning());
    }

    // Manual triggers (1-4)
    if (ctx.key(GLFW_KEY_1).pressed) {
        kick.trigger();
        kickDecay = 1.0f;
    }
    if (ctx.key(GLFW_KEY_2).pressed) {
        snare.trigger();
        snareDecay = 1.0f;
    }
    if (ctx.key(GLFW_KEY_3).pressed) {
        hihat.trigger();
        hihatDecay = 1.0f;
    }
    if (ctx.key(GLFW_KEY_4).pressed) {
        clap.trigger();
        clapDecay = 1.0f;
    }

    // BPM adjustment
    float bpm = clock.getBpm();
    if (ctx.key(GLFW_KEY_UP).pressed) {
        bpm = std::min(bpm + 5.0f, 300.0f);
        clock.bpm(bpm);
        printStatus(bpm, clock.isRunning());
    }
    if (ctx.key(GLFW_KEY_DOWN).pressed) {
        bpm = std::max(bpm - 5.0f, 60.0f);
        clock.bpm(bpm);
        printStatus(bpm, clock.isRunning());
    }

    // Pattern change
    if (ctx.key(GLFW_KEY_RIGHT).pressed) {
        currentPattern = (currentPattern + 1) % numPatterns;
        kickSeq.setPattern(kickPatterns[currentPattern]);
        snareSeq.setPattern(snarePatterns[currentPattern]);
        hihatSeq.setPattern(hihatPatterns[currentPattern]);
        clapSeq.setPattern(clapPatterns[currentPattern]);
        printStatus(bpm, clock.isRunning());
    }
    if (ctx.key(GLFW_KEY_LEFT).pressed) {
        currentPattern = (currentPattern - 1 + numPatterns) % numPatterns;
        kickSeq.setPattern(kickPatterns[currentPattern]);
        snareSeq.setPattern(snarePatterns[currentPattern]);
        hihatSeq.setPattern(hihatPatterns[currentPattern]);
        clapSeq.setPattern(clapPatterns[currentPattern]);
        printStatus(bpm, clock.isRunning());
    }

    // Toggle Euclidean mode
    if (ctx.key(GLFW_KEY_E).pressed) {
        useEuclidean = !useEuclidean;
        // Reset all sequencers
        kickSeq.reset();
        snareSeq.reset();
        hihatSeq.reset();
        clapSeq.reset();
        kickEucl.reset();
        snareEucl.reset();
        hihatEucl.reset();
        clapEucl.reset();
        printStatus(bpm, clock.isRunning());
    }

    // Swing adjustment
    if (ctx.key(GLFW_KEY_S).pressed) {
        float swing = clock.getSwing();
        swing = std::fmod(swing + 0.25f, 1.0f);
        clock.swing(swing);
        std::cout << "\n[Swing: " << static_cast<int>(swing * 100) << "%]" << std::endl;
        printStatus(bpm, clock.isRunning());
    }

    // =========================================================================
    // Sequencer Logic
    // =========================================================================

    if (clock.triggered()) {
        bool triggerKick = false;
        bool triggerSnare = false;
        bool triggerHihat = false;
        bool triggerClap = false;

        if (useEuclidean) {
            // Euclidean mode
            kickEucl.advance();
            snareEucl.advance();
            hihatEucl.advance();
            clapEucl.advance();

            triggerKick = kickEucl.triggered();
            triggerSnare = snareEucl.triggered();
            triggerHihat = hihatEucl.triggered();
            triggerClap = clapEucl.triggered();
        } else {
            // Pattern mode
            kickSeq.advance();
            snareSeq.advance();
            hihatSeq.advance();
            clapSeq.advance();

            triggerKick = kickSeq.triggered();
            triggerSnare = snareSeq.triggered();
            triggerHihat = hihatSeq.triggered();
            triggerClap = clapSeq.triggered();
        }

        // Trigger drums
        if (triggerKick) {
            kick.trigger();
            kickDecay = 1.0f;
        }
        if (triggerSnare) {
            snare.trigger();
            snareDecay = 1.0f;
        }
        if (triggerHihat) {
            hihat.trigger();
            hihatDecay = 1.0f;
        }
        if (triggerClap) {
            clap.trigger();
            clapDecay = 1.0f;
        }
    }

    // =========================================================================
    // Visual Feedback
    // =========================================================================

    float decayRate = 1.0f - ctx.dt() * 8.0f;  // Fast decay

    kickDecay *= decayRate;
    snareDecay *= decayRate;
    hihatDecay *= decayRate;
    clapDecay *= decayRate;

    // Update visualizer sizes based on hit intensity
    kickVis.size(0.08f + kickDecay * 0.15f);
    snareVis.size(0.06f + snareDecay * 0.12f);
    hihatVis.size(0.04f + hihatDecay * 0.08f);
    clapVis.size(0.05f + clapDecay * 0.1f);

    // Pulse colors on hit - using Color constants with dynamic alpha
    kickVis.color(Color::Tomato.withAlpha(0.3f + kickDecay * 0.7f));
    snareVis.color(Color::Gold.withAlpha(0.3f + snareDecay * 0.7f));
    hihatVis.color(Color::Cyan.withAlpha(0.3f + hihatDecay * 0.7f));
    clapVis.color(Color::Orchid.withAlpha(0.3f + clapDecay * 0.7f));
}

VIVID_CHAIN(setup, update)
