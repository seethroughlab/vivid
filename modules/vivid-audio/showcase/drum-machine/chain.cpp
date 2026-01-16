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
#include <vivid/gui/imgui.h>
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
    clock.bpm = 120.0f;
    clock.division(ClockDiv::Sixteenth);
    clock.swing = 0.0f;

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
    kickEucl.steps = 16;
    kickEucl.hits = 4;
    kickEucl.rotation = 0;

    snareEucl.steps = 16;
    snareEucl.hits = 2;
    snareEucl.rotation = 4;

    hihatEucl.steps = 16;
    hihatEucl.hits = 8;
    hihatEucl.rotation = 0;

    clapEucl.steps = 16;
    clapEucl.hits = 3;
    clapEucl.rotation = 2;

    // =========================================================================
    // Drum Voices
    // =========================================================================

    // 808-style kick
    auto& kick = chain.add<Kick>("kick");
    kick.pitch = 50.0f;
    kick.pitchEnv = 120.0f;
    kick.pitchDecay = 0.08f;
    kick.decay = 0.2f;  // Shorter tail
    kick.click = 0.4f;
    kick.drive = 0.2f;
    kick.volume = 0.9f;

    // Punchy snare
    auto& snare = chain.add<Snare>("snare");
    snare.tone = 0.4f;
    snare.noise = 0.7f;
    snare.pitch = 180.0f;
    snare.toneDecay = 0.08f;
    snare.noiseDecay = 0.15f;
    snare.snappy = 0.6f;
    snare.volume = 0.7f;

    // Closed hi-hat
    auto& hihat = chain.add<HiHat>("hihat");
    hihat.decay = 0.05f;
    hihat.tone = 0.7f;
    hihat.ring = 0.4f;
    hihat.volume = 0.4f;

    // Hand clap
    auto& clap = chain.add<Clap>("clap");
    clap.decay = 0.25f;
    clap.tone = 0.5f;
    clap.spread = 0.6f;
    clap.volume = 0.5f;

    // =========================================================================
    // Audio Output
    // =========================================================================

    // Mix all drums together
    auto& mixer = chain.add<AudioMixer>("mixer");
    mixer.setInput(0, "kick");
    mixer.setGain(0, 1.0f);
    mixer.setInput(1, "snare");
    mixer.setGain(1, 0.8f);
    mixer.setInput(2, "hihat");
    mixer.setGain(2, 0.5f);
    mixer.setInput(3, "clap");
    mixer.setGain(3, 0.6f);
    mixer.volume = 0.8f;

    auto& audioOut = chain.add<AudioOutput>("audioOut");
    audioOut.setInput("mixer");
    audioOut.setVolume(0.5f);
    chain.audioOutput("audioOut");

    // =========================================================================
    // Visual Feedback - Drum hit visualization
    // =========================================================================

    // Background
    auto& bg = chain.add<SolidColor>("bg");
    bg.color.set(0.05f, 0.05f, 0.08f, 1.0f);

    // Kick visualizer (bottom) - red/orange
    auto& kickVis = chain.add<Shape>("kickVis");
    kickVis.type = ShapeType::Ellipse;
    kickVis.position.set(0.5f, 0.3f);
    kickVis.size.set(0.15f, 0.15f);
    kickVis.color.set(1.0f, 0.39f, 0.28f, 1.0f);  // Tomato
    kickVis.softness = 0.1f;

    // Snare visualizer (center-left) - yellow/gold
    auto& snareVis = chain.add<Shape>("snareVis");
    snareVis.type = ShapeType::Ellipse;
    snareVis.position.set(0.35f, 0.5f);
    snareVis.size.set(0.12f, 0.12f);
    snareVis.color.set(1.0f, 0.84f, 0.0f, 1.0f);  // Gold
    snareVis.softness = 0.1f;

    // Hi-hat visualizer (center-right) - cyan
    auto& hihatVis = chain.add<Shape>("hihatVis");
    hihatVis.type = ShapeType::Ellipse;
    hihatVis.position.set(0.65f, 0.5f);
    hihatVis.size.set(0.08f, 0.08f);
    hihatVis.color.set(0.0f, 1.0f, 1.0f, 1.0f);  // Cyan
    hihatVis.softness = 0.1f;

    // Clap visualizer (top) - magenta/violet
    auto& clapVis = chain.add<Shape>("clapVis");
    clapVis.type = ShapeType::Ellipse;
    clapVis.position.set(0.5f, 0.7f);
    clapVis.size.set(0.1f, 0.1f);
    clapVis.color.set(0.85f, 0.44f, 0.84f, 1.0f);  // Orchid
    clapVis.softness = 0.1f;

    // Composite all layers
    auto& comp = chain.add<Composite>("comp");
    comp.input(0, "bg");
    comp.input(1, "kickVis");
    comp.input(2, "snareVis");
    comp.input(3, "hihatVis");
    comp.input(4, "clapVis");
    comp.mode = BlendMode::Add;

    chain.output("comp");

    // =========================================================================
    // Trigger Connections (for chain visualizer)
    // =========================================================================

    // Sequencers are triggered by Clock
    kickSeq.setTriggerSource("clock");
    snareSeq.setTriggerSource("clock");
    hihatSeq.setTriggerSource("clock");
    clapSeq.setTriggerSource("clock");

    // Euclidean sequencers also triggered by Clock
    kickEucl.setTriggerSource("clock");
    snareEucl.setTriggerSource("clock");
    hihatEucl.setTriggerSource("clock");
    clapEucl.setTriggerSource("clock");

    // Drums are triggered by their respective sequencers
    kick.setTriggerSource("kickSeq");
    snare.setTriggerSource("snareSeq");
    hihat.setTriggerSource("hihatSeq");
    clap.setTriggerSource("clapSeq");

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

// UI state for ImGui panel
static bool g_kickMute = false;
static bool g_snareMute = false;
static bool g_hihatMute = false;
static bool g_clapMute = false;
static float g_kickVol = 0.9f;
static float g_snareVol = 0.7f;
static float g_hihatVol = 0.4f;
static float g_clapVol = 0.5f;

// Kick parameters
static float g_kickPitch = 50.0f;
static float g_kickPitchEnv = 120.0f;
static float g_kickPitchDecay = 0.08f;
static float g_kickDecay = 0.2f;  // Shorter tail
static float g_kickClick = 0.4f;
static float g_kickDrive = 0.2f;

// Snare parameters
static float g_snareTone = 0.4f;
static float g_snareNoise = 0.7f;
static float g_snarePitch = 180.0f;
static float g_snareToneDecay = 0.08f;
static float g_snareNoiseDecay = 0.15f;
static float g_snareSnappy = 0.6f;

// HiHat parameters
static float g_hihatDecay = 0.05f;
static float g_hihatTone = 0.7f;
static float g_hihatRing = 0.4f;

// Clap parameters
static float g_clapDecay = 0.25f;
static float g_clapTone = 0.5f;
static float g_clapSpread = 0.6f;

// Master volume
static float g_masterVolume = 0.5f;

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

    auto& audioOut = chain.get<AudioOutput>("audioOut");

    // =========================================================================
    // ImGui Composition Panel
    // =========================================================================

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280, 500), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Drum Machine")) {
        // Transport
        ImGui::SeparatorText("Transport");
        bool running = clock.isRunning();
        if (ImGui::Button(running ? "Stop" : "Play", ImVec2(60, 0))) {
            if (running) clock.stop(); else clock.start();
            printStatus(static_cast<float>(clock.bpm), clock.isRunning());
        }
        ImGui::SameLine();
        float bpm = static_cast<float>(clock.bpm);
        if (ImGui::SliderFloat("BPM", &bpm, 60.0f, 300.0f, "%.0f")) {
            clock.bpm = bpm;
        }

        if (ImGui::SliderFloat("Master", &g_masterVolume, 0.0f, 1.0f, "%.2f")) {
            audioOut.setVolume(g_masterVolume);
        }

        // Mode
        ImGui::SeparatorText("Sequencer");
        if (ImGui::Checkbox("Euclidean Mode", &useEuclidean)) {
            // Reset sequencers on mode change
            kickSeq.reset(); snareSeq.reset(); hihatSeq.reset(); clapSeq.reset();
            kickEucl.reset(); snareEucl.reset(); hihatEucl.reset(); clapEucl.reset();

            // Switch drum trigger sources based on mode (use pointers at runtime)
            if (useEuclidean) {
                kick.setTriggerSource(&kickEucl);
                snare.setTriggerSource(&snareEucl);
                hihat.setTriggerSource(&hihatEucl);
                clap.setTriggerSource(&clapEucl);
            } else {
                kick.setTriggerSource(&kickSeq);
                snare.setTriggerSource(&snareSeq);
                hihat.setTriggerSource(&hihatSeq);
                clap.setTriggerSource(&clapSeq);
            }
            printStatus(static_cast<float>(clock.bpm), clock.isRunning());
        }

        if (!useEuclidean) {
            const char* patterns[] = {"Four on Floor", "Half Time", "Syncopated", "Breakbeat"};
            if (ImGui::Combo("Pattern", &currentPattern, patterns, 4)) {
                kickSeq.setPattern(kickPatterns[currentPattern]);
                snareSeq.setPattern(snarePatterns[currentPattern]);
                hihatSeq.setPattern(hihatPatterns[currentPattern]);
                clapSeq.setPattern(clapPatterns[currentPattern]);
            }
        }

        float swingPercent = static_cast<float>(clock.swing) * 100.0f;
        if (ImGui::SliderFloat("Swing", &swingPercent, 0.0f, 100.0f, "%.0f%%")) {
            clock.swing = swingPercent / 100.0f;
        }

        // Instruments
        ImGui::SeparatorText("Instruments");

        // Kick
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.4f, 0.15f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.5f, 0.2f, 0.14f, 1.0f));
        if (ImGui::CollapsingHeader("Kick")) {
            ImGui::Checkbox("Mute##kick", &g_kickMute);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150);
            ImGui::SliderFloat("Vol##kick", &g_kickVol, 0.0f, 1.0f, "%.2f");

            ImGui::SetNextItemWidth(200);
            ImGui::SliderFloat("Decay", &g_kickDecay, 0.05f, 1.0f, "%.2f");
            ImGui::SetNextItemWidth(200);
            ImGui::SliderFloat("Pitch", &g_kickPitch, 30.0f, 100.0f, "%.0f Hz");
            ImGui::SetNextItemWidth(200);
            ImGui::SliderFloat("Pitch Env", &g_kickPitchEnv, 50.0f, 300.0f, "%.0f Hz");
            ImGui::SetNextItemWidth(200);
            ImGui::SliderFloat("Click", &g_kickClick, 0.0f, 1.0f, "%.2f");
            ImGui::SetNextItemWidth(200);
            ImGui::SliderFloat("Drive", &g_kickDrive, 0.0f, 1.0f, "%.2f");
        }
        ImGui::PopStyleColor(2);

        // Snare
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.4f, 0.33f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.5f, 0.42f, 0.0f, 1.0f));
        if (ImGui::CollapsingHeader("Snare")) {
            ImGui::Checkbox("Mute##snare", &g_snareMute);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150);
            ImGui::SliderFloat("Vol##snare", &g_snareVol, 0.0f, 1.0f, "%.2f");

            ImGui::SetNextItemWidth(200);
            ImGui::SliderFloat("Tone##snare", &g_snareTone, 0.0f, 1.0f, "%.2f");
            ImGui::SetNextItemWidth(200);
            ImGui::SliderFloat("Noise##snare", &g_snareNoise, 0.0f, 1.0f, "%.2f");
            ImGui::SetNextItemWidth(200);
            ImGui::SliderFloat("Snappy", &g_snareSnappy, 0.0f, 1.0f, "%.2f");
            ImGui::SetNextItemWidth(200);
            ImGui::SliderFloat("Tone Decay", &g_snareToneDecay, 0.01f, 0.5f, "%.2f");
            ImGui::SetNextItemWidth(200);
            ImGui::SliderFloat("Noise Decay", &g_snareNoiseDecay, 0.01f, 0.5f, "%.2f");
        }
        ImGui::PopStyleColor(2);

        // HiHat
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.0f, 0.35f, 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.0f, 0.45f, 0.45f, 1.0f));
        if (ImGui::CollapsingHeader("HiHat")) {
            ImGui::Checkbox("Mute##hihat", &g_hihatMute);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150);
            ImGui::SliderFloat("Vol##hihat", &g_hihatVol, 0.0f, 1.0f, "%.2f");

            ImGui::SetNextItemWidth(200);
            ImGui::SliderFloat("Decay##hihat", &g_hihatDecay, 0.01f, 0.5f, "%.2f");
            ImGui::SetNextItemWidth(200);
            ImGui::SliderFloat("Tone##hihat", &g_hihatTone, 0.0f, 1.0f, "%.2f");
            ImGui::SetNextItemWidth(200);
            ImGui::SliderFloat("Ring", &g_hihatRing, 0.0f, 1.0f, "%.2f");
        }
        ImGui::PopStyleColor(2);

        // Clap
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.35f, 0.18f, 0.34f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.43f, 0.22f, 0.42f, 1.0f));
        if (ImGui::CollapsingHeader("Clap")) {
            ImGui::Checkbox("Mute##clap", &g_clapMute);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(150);
            ImGui::SliderFloat("Vol##clap", &g_clapVol, 0.0f, 1.0f, "%.2f");

            ImGui::SetNextItemWidth(200);
            ImGui::SliderFloat("Decay##clap", &g_clapDecay, 0.05f, 0.5f, "%.2f");
            ImGui::SetNextItemWidth(200);
            ImGui::SliderFloat("Tone##clap", &g_clapTone, 0.0f, 1.0f, "%.2f");
            ImGui::SetNextItemWidth(200);
            ImGui::SliderFloat("Spread", &g_clapSpread, 0.0f, 1.0f, "%.2f");
        }
        ImGui::PopStyleColor(2);

        // Apply all parameters
        kick.volume = g_kickMute ? 0.0f : g_kickVol;
        kick.pitch = g_kickPitch;
        kick.pitchEnv = g_kickPitchEnv;
        kick.pitchDecay = g_kickPitchDecay;
        kick.decay = g_kickDecay;
        kick.click = g_kickClick;
        kick.drive = g_kickDrive;

        snare.volume = g_snareMute ? 0.0f : g_snareVol;
        snare.tone = g_snareTone;
        snare.noise = g_snareNoise;
        snare.pitch = g_snarePitch;
        snare.toneDecay = g_snareToneDecay;
        snare.noiseDecay = g_snareNoiseDecay;
        snare.snappy = g_snareSnappy;

        hihat.volume = g_hihatMute ? 0.0f : g_hihatVol;
        hihat.decay = g_hihatDecay;
        hihat.tone = g_hihatTone;
        hihat.ring = g_hihatRing;

        clap.volume = g_clapMute ? 0.0f : g_clapVol;
        clap.decay = g_clapDecay;
        clap.tone = g_clapTone;
        clap.spread = g_clapSpread;
    }
    ImGui::End();

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
        printStatus(static_cast<float>(clock.bpm), clock.isRunning());
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
    float bpmVal = static_cast<float>(clock.bpm);
    if (ctx.key(GLFW_KEY_UP).pressed) {
        bpmVal = std::min(bpmVal + 5.0f, 300.0f);
        clock.bpm = bpmVal;
        printStatus(bpmVal, clock.isRunning());
    }
    if (ctx.key(GLFW_KEY_DOWN).pressed) {
        bpmVal = std::max(bpmVal - 5.0f, 60.0f);
        clock.bpm = bpmVal;
        printStatus(bpmVal, clock.isRunning());
    }

    // Pattern change
    if (ctx.key(GLFW_KEY_RIGHT).pressed) {
        currentPattern = (currentPattern + 1) % numPatterns;
        kickSeq.setPattern(kickPatterns[currentPattern]);
        snareSeq.setPattern(snarePatterns[currentPattern]);
        hihatSeq.setPattern(hihatPatterns[currentPattern]);
        clapSeq.setPattern(clapPatterns[currentPattern]);
        printStatus(bpmVal, clock.isRunning());
    }
    if (ctx.key(GLFW_KEY_LEFT).pressed) {
        currentPattern = (currentPattern - 1 + numPatterns) % numPatterns;
        kickSeq.setPattern(kickPatterns[currentPattern]);
        snareSeq.setPattern(snarePatterns[currentPattern]);
        hihatSeq.setPattern(hihatPatterns[currentPattern]);
        clapSeq.setPattern(clapPatterns[currentPattern]);
        printStatus(bpmVal, clock.isRunning());
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
        printStatus(bpmVal, clock.isRunning());
    }

    // Swing adjustment
    if (ctx.key(GLFW_KEY_S).pressed) {
        float swingVal = static_cast<float>(clock.swing);
        swingVal = std::fmod(swingVal + 0.25f, 1.0f);
        clock.swing = swingVal;
        std::cout << "\n[Swing: " << static_cast<int>(swingVal * 100) << "%]" << std::endl;
        printStatus(bpmVal, clock.isRunning());
    }

    // =========================================================================
    // Audio-thread triggering - no manual advance/trigger needed!
    // Sequencers and Euclideans advance via setTriggerSource("clock")
    // Drums trigger via setTriggerSource("kickSeq") etc.
    // =========================================================================

    // Visual feedback - check if drums triggered (thread-safe)
    bool triggerKick = useEuclidean ? kickEucl.triggered() : kickSeq.triggered();
    bool triggerSnare = useEuclidean ? snareEucl.triggered() : snareSeq.triggered();
    bool triggerHihat = useEuclidean ? hihatEucl.triggered() : hihatSeq.triggered();
    bool triggerClap = useEuclidean ? clapEucl.triggered() : clapSeq.triggered();

    if (triggerKick) kickDecay = 1.0f;
    if (triggerSnare) snareDecay = 1.0f;
    if (triggerHihat) hihatDecay = 1.0f;
    if (triggerClap) clapDecay = 1.0f;

    // =========================================================================
    // Visual Feedback
    // =========================================================================

    float decayRate = 1.0f - ctx.dt() * 8.0f;  // Fast decay

    kickDecay *= decayRate;
    snareDecay *= decayRate;
    hihatDecay *= decayRate;
    clapDecay *= decayRate;

    // Update visualizer sizes based on hit intensity
    kickVis.size.set(0.08f + kickDecay * 0.15f, 0.08f + kickDecay * 0.15f);
    snareVis.size.set(0.06f + snareDecay * 0.12f, 0.06f + snareDecay * 0.12f);
    hihatVis.size.set(0.04f + hihatDecay * 0.08f, 0.04f + hihatDecay * 0.08f);
    clapVis.size.set(0.05f + clapDecay * 0.1f, 0.05f + clapDecay * 0.1f);

    // Pulse colors on hit
    kickVis.color.set(1.0f, 0.39f, 0.28f, 0.3f + kickDecay * 0.7f);
    snareVis.color.set(1.0f, 0.84f, 0.0f, 0.3f + snareDecay * 0.7f);
    hihatVis.color.set(0.0f, 1.0f, 1.0f, 0.3f + hihatDecay * 0.7f);
    clapVis.color.set(0.85f, 0.44f, 0.84f, 0.3f + clapDecay * 0.7f);
}

// Compact window for drum machine UI
VIVID_CHAIN_CONFIG(setup, update, (vivid::ChainConfig{
    .windowWidth = 1024,
    .windowHeight = 600,
    .resizable = false
}))
