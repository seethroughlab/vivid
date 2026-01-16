// Drum Showcase - Visual Step Sequencer
// Demonstrates all 9 drum operators with visual sequencer grid, rolls, skips, and glitch effects
//
// Features:
// - 16-step visual sequencer for each drum
// - Random skips (probability to skip a hit)
// - Random rolls (stutter repeats on hits)
// - Master glitch effects on output
// - ImGui control panel

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <vivid/audio_output.h>
#include <vivid/gui/imgui.h>
#include <cmath>
#include <random>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

// ============================================================================
// PATTERN DATA
// ============================================================================

// 9 drums x 16 steps
static uint16_t g_patterns[9] = {
    0x1111,  // Kick: X...X...X...X...
    0x0808,  // Snare: ....X.......X...
    0xFFFF,  // HiHat: XXXXXXXXXXXXXXXX
    0x0808,  // Clap: ....X.......X...
    0x0000,  // Tom: (empty)
    0x0000,  // Cymbal: (empty)
    0x0000,  // FMDrum: (empty)
    0x0000,  // Clang: (empty)
    0x0000,  // Stack: (empty)
};

// Pattern presets
static const uint16_t PRESET_PATTERNS[4][9] = {
    // A: Standard 4-on-floor
    {0x1111, 0x0808, 0xFFFF, 0x0808, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    // B: Extended kit
    {0x1111, 0x0808, 0x5555, 0x0000, 0x2020, 0x8000, 0x0000, 0x0202, 0x0000},
    // C: FM/Experimental
    {0x1119, 0x0000, 0xAAAA, 0x0000, 0x0000, 0x0000, 0x2424, 0x0000, 0x0808},
    // D: Full kit
    {0x1111, 0x0808, 0xFFFF, 0x0808, 0x0044, 0x0001, 0x2020, 0x0400, 0x4000},
};

// Drum names and colors
static const char* DRUM_NAMES[] = {"Kick", "Snare", "HiHat", "Clap", "Tom", "Cymbal", "FMDrum", "Clang", "Stack"};
static const ImU32 DRUM_COLORS[] = {
    IM_COL32(255, 80, 60, 255),    // Kick - Red
    IM_COL32(255, 200, 60, 255),   // Snare - Yellow
    IM_COL32(80, 230, 230, 255),   // HiHat - Cyan
    IM_COL32(255, 130, 200, 255),  // Clap - Pink
    IM_COL32(150, 100, 255, 255),  // Tom - Purple
    IM_COL32(255, 230, 130, 255),  // Cymbal - Gold
    IM_COL32(60, 255, 130, 255),   // FMDrum - Green
    IM_COL32(255, 150, 60, 255),   // Clang - Orange
    IM_COL32(200, 200, 200, 255),  // Stack - White
};

// ============================================================================
// STATE
// ============================================================================

static int g_currentStep = 0;
static bool g_isPlaying = true;
static int g_currentPreset = 0;
static float g_bpm = 120.0f;
static float g_swing = 0.0f;

// Randomness parameters
static float g_skipChance = 0.0f;      // Chance to skip a hit
static float g_rollChance = 0.1f;      // Chance to roll (stutter) a hit
static int g_rollCount = 3;            // Number of repeats in a roll

// Glitch parameters
static float g_glitchAmount = 0.0f;    // Master glitch intensity
static float g_repeatChance = 0.1f;
static float g_reverseChance = 0.08f;
static float g_stutterChance = 0.1f;
static float g_tapeChance = 0.05f;

// Visual state
static float g_drumDecay[9] = {0};
static float g_stepFlash[16] = {0};
static bool g_rollActive[9] = {false};
static int g_rollsRemaining[9] = {0};

// Random generator
static std::mt19937 g_rng(std::random_device{}());
static std::uniform_real_distribution<float> g_dist(0.0f, 1.0f);
static float randomFloat() { return g_dist(g_rng); }

// ============================================================================
// SETUP
// ============================================================================

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // ----- MASTER CLOCK -----
    auto& clock = chain.add<Clock>("clock");
    clock.bpm = g_bpm;
    clock.division(ClockDiv::Sixteenth);
    clock.swing = g_swing;
    clock.start();

    // =========================================================================
    // DRUM VOICES
    // =========================================================================

    // 1. KICK
    auto& kick = chain.add<Kick>("kick");
    kick.pitch = 55.0f;
    kick.pitchEnv = 120.0f;
    kick.decay = 0.4f;
    kick.click = 0.5f;
    kick.drive = 0.3f;
    kick.overtones = 0.2f;
    kick.volume = 0.9f;

    // 2. SNARE
    auto& snare = chain.add<Snare>("snare");
    snare.tone = 0.5f;
    snare.noise = 0.7f;
    snare.pitch = 200.0f;
    snare.snappy = 0.8f;
    snare.color = 0.6f;
    snare.volume = 0.75f;

    // 3. HIHAT
    auto& hihat = chain.add<HiHat>("hihat");
    hihat.decay = 0.05f;
    hihat.tone = 0.4f;
    hihat.ring = 0.3f;
    hihat.volume = 0.5f;

    // 4. CLAP
    auto& clap = chain.add<Clap>("clap");
    clap.decay = 0.2f;
    clap.sloppy = 0.04f;
    clap.tone = 0.6f;
    clap.tail = 0.3f;
    clap.stereoWidth = 0.4f;
    clap.volume = 0.65f;

    // 5. TOM
    auto& tom = chain.add<Tom>("tom");
    tom.pitch = 120.0f;
    tom.bend = 0.6f;
    tom.decay = 0.35f;
    tom.tone = 0.5f;
    tom.volume = 0.7f;

    // 6. CYMBAL
    auto& cymbal = chain.add<Cymbal>("cymbal");
    cymbal.pitch = 1.0f;
    cymbal.decay = 2.5f;
    cymbal.shimmer = 0.3f;
    cymbal.volume = 0.6f;

    // 7. FMDRUM
    auto& fmdrum = chain.add<FMDrum>("fmdrum");
    fmdrum.pitch = 180.0f;
    fmdrum.ratio = 2.5f;
    fmdrum.amount = 0.7f;
    fmdrum.feedback = 0.2f;
    fmdrum.decay = 0.3f;
    fmdrum.volume = 0.6f;

    // 8. CLANG
    auto& clang = chain.add<Clang>("clang");
    clang.pitch = 800.0f;
    clang.toneA = 0.6f;
    clang.toneB = 0.4f;
    clang.ratio = 1.47f;
    clang.decay = 0.15f;
    clang.volume = 0.5f;

    // 9. DRUMSTACK
    auto& stack = chain.add<DrumStack>("stack");
    stack.mix1 = 0.8f;
    stack.mix2 = 0.5f;
    stack.volume = 0.7f;

    // ----- DRY MIX -----
    auto& dryMix = chain.add<AudioMixer>("dryMix");
    dryMix.setInput(0, "kick");
    dryMix.setInput(1, "snare");
    dryMix.setInput(2, "hihat");
    dryMix.setInput(3, "clap");
    dryMix.setInput(4, "tom");
    dryMix.setInput(5, "cymbal");
    dryMix.setInput(6, "fmdrum");
    dryMix.setInput(7, "clang");
    dryMix.setInput(8, "stack");
    dryMix.volume = 0.7f;

    // ----- GLITCH EFFECT -----
    auto& glitch = chain.add<Glitch>("glitch");
    glitch.input("dryMix");
    glitch.bpm = g_bpm;
    glitch.triggerDiv(ClockDiv::Eighth);
    glitch.repeatChance = 0.0f;
    glitch.reverseChance = 0.0f;
    glitch.stutterChance = 0.0f;
    glitch.scratchChance = 0.0f;
    glitch.tapeChance = 0.0f;
    glitch.shiftChance = 0.0f;
    glitch.mix = 1.0f;

    // ----- AUDIO OUTPUT -----
    auto& output = chain.add<AudioOutput>("audio_out");
    output.setInput("glitch");
    chain.audioOutput("audio_out");

    // =========================================================================
    // VISUALS - Canvas-based step sequencer
    // =========================================================================

    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(1280, 720);
    canvas.clear(0.06f, 0.06f, 0.08f, 1.0f);

    chain.output("canvas");
}

// ============================================================================
// DRAW SEQUENCER GRID
// ============================================================================

void drawSequencerGrid(Canvas& canvas, float x, float y, float w, float h) {
    const int numDrums = 9;
    const int numSteps = 16;

    float cellW = w / numSteps;
    float cellH = h / numDrums;
    float padding = 2.0f;

    // Draw grid background
    canvas.fillStyle(0.1f, 0.1f, 0.12f, 1.0f);
    canvas.fillRect(x, y, w, h);

    // Draw cells
    for (int drum = 0; drum < numDrums; drum++) {
        for (int step = 0; step < numSteps; step++) {
            float cx = x + step * cellW + padding;
            float cy = y + drum * cellH + padding;
            float cw = cellW - padding * 2;
            float ch = cellH - padding * 2;

            bool isActive = (g_patterns[drum] >> step) & 1;
            bool isCurrentStep = (step == g_currentStep);

            // Extract color components
            ImU32 col = DRUM_COLORS[drum];
            float r = ((col >> 0) & 0xFF) / 255.0f;
            float gr = ((col >> 8) & 0xFF) / 255.0f;
            float b = ((col >> 16) & 0xFF) / 255.0f;

            if (isActive) {
                // Active step - full color with decay brightness
                float brightness = 0.4f + g_drumDecay[drum] * 0.6f;
                if (isCurrentStep && g_isPlaying) brightness = 1.0f;
                canvas.fillStyle(r * brightness, gr * brightness, b * brightness, 1.0f);
                canvas.fillRect(cx, cy, cw, ch);
            } else {
                // Inactive step - dim
                float dim = 0.15f;
                if (isCurrentStep && g_isPlaying) dim = 0.3f;
                canvas.fillStyle(r * dim, gr * dim, b * dim, 0.5f);
                canvas.fillRect(cx, cy, cw, ch);
            }

            // Roll indicator (small circle in corner)
            if (isActive && g_rollActive[drum] && step == g_currentStep) {
                float dotR = 4.0f;
                canvas.fillStyle(1.0f, 1.0f, 1.0f, 0.9f);
                canvas.fillCircle(cx + cw - dotR - 2, cy + dotR + 2, dotR);
            }
        }
    }

    // Draw playhead line
    if (g_isPlaying) {
        float phX = x + g_currentStep * cellW + cellW * 0.5f;
        canvas.fillStyle(1.0f, 1.0f, 1.0f, 0.8f);
        canvas.fillRect(phX - 1, y, 2, h);
    }

    // Draw step flash effects
    for (int step = 0; step < numSteps; step++) {
        if (g_stepFlash[step] > 0.01f) {
            float fx = x + step * cellW;
            canvas.fillStyle(1.0f, 1.0f, 1.0f, g_stepFlash[step] * 0.3f);
            canvas.fillRect(fx, y, cellW, h);
        }
    }
}

// ============================================================================
// UPDATE
// ============================================================================

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& clock = chain.get<Clock>("clock");
    auto& canvas = chain.get<Canvas>("canvas");

    // Get drum operators
    Kick& kick = chain.get<Kick>("kick");
    Snare& snare = chain.get<Snare>("snare");
    HiHat& hihat = chain.get<HiHat>("hihat");
    Clap& clap = chain.get<Clap>("clap");
    Tom& tom = chain.get<Tom>("tom");
    Cymbal& cymbal = chain.get<Cymbal>("cymbal");
    FMDrum& fmdrum = chain.get<FMDrum>("fmdrum");
    Clang& clang = chain.get<Clang>("clang");
    DrumStack& stack = chain.get<DrumStack>("stack");

    // Array for easy access
    AudioOperator* drums[] = {&kick, &snare, &hihat, &clap, &tom, &cymbal, &fmdrum, &clang, &stack};

    // Get glitch
    auto& glitch = chain.get<Glitch>("glitch");

    // Update glitch parameters based on intensity
    glitch.bpm = g_bpm;
    glitch.repeatChance = g_repeatChance * g_glitchAmount;
    glitch.reverseChance = g_reverseChance * g_glitchAmount;
    glitch.stutterChance = g_stutterChance * g_glitchAmount;
    glitch.tapeChance = g_tapeChance * g_glitchAmount;

    // =========================================================================
    // ImGui Control Panel
    // =========================================================================

    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, 500), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Drum Sequencer")) {
        // Transport
        ImGui::SeparatorText("Transport");

        if (ImGui::Button(g_isPlaying ? "Stop" : "Play", ImVec2(80, 30))) {
            g_isPlaying = !g_isPlaying;
            if (g_isPlaying) clock.start();
            else clock.stop();
        }

        ImGui::SameLine();
        if (ImGui::SliderFloat("BPM", &g_bpm, 60.0f, 200.0f, "%.0f")) {
            clock.bpm = g_bpm;
        }

        ImGui::SliderFloat("Swing", &g_swing, 0.0f, 0.5f, "%.2f");
        clock.swing = g_swing;

        // Pattern presets
        ImGui::SeparatorText("Patterns");
        const char* presetNames[] = {"A: Standard", "B: Extended", "C: Experimental", "D: Full Kit"};
        if (ImGui::Combo("Preset", &g_currentPreset, presetNames, 4)) {
            for (int i = 0; i < 9; i++) {
                g_patterns[i] = PRESET_PATTERNS[g_currentPreset][i];
            }
        }

        // Randomness
        ImGui::SeparatorText("Randomness");
        ImGui::SliderFloat("Skip Chance", &g_skipChance, 0.0f, 0.5f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Roll Chance", &g_rollChance, 0.0f, 0.5f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderInt("Roll Count", &g_rollCount, 2, 8);

        // Glitch
        ImGui::SeparatorText("Glitch FX");
        ImGui::SliderFloat("Amount", &g_glitchAmount, 0.0f, 1.0f, "%.0f%%");

        if (g_glitchAmount > 0.01f) {
            ImGui::SliderFloat("Repeat", &g_repeatChance, 0.0f, 0.5f, "%.0f%%");
            ImGui::SliderFloat("Reverse", &g_reverseChance, 0.0f, 0.3f, "%.0f%%");
            ImGui::SliderFloat("Stutter", &g_stutterChance, 0.0f, 0.4f, "%.0f%%");
            ImGui::SliderFloat("Tape", &g_tapeChance, 0.0f, 0.2f, "%.0f%%");
        }

        // Manual triggers
        ImGui::SeparatorText("Manual Triggers");
        for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 3; col++) {
                int i = row * 3 + col;
                if (col > 0) ImGui::SameLine();

                ImGui::PushID(i);
                ImU32 c = DRUM_COLORS[i];
                ImGui::PushStyleColor(ImGuiCol_Button, c);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(
                    std::min(255, (int)((c & 0xFF) * 1.2f)),
                    std::min(255, (int)(((c >> 8) & 0xFF) * 1.2f)),
                    std::min(255, (int)(((c >> 16) & 0xFF) * 1.2f)),
                    255
                ));

                if (ImGui::Button(DRUM_NAMES[i], ImVec2(70, 35))) {
                    drums[i]->trigger();
                    g_drumDecay[i] = 1.0f;
                }

                ImGui::PopStyleColor(2);
                ImGui::PopID();
            }
        }

        // Step info
        ImGui::SeparatorText("Info");
        ImGui::Text("Step: %d/16", g_currentStep + 1);
    }
    ImGui::End();

    // =========================================================================
    // SEQUENCER LOGIC
    // =========================================================================

    // Process rolls (handle ongoing rolls even between clock ticks)
    float rollInterval = 60.0f / (g_bpm * 4.0f);  // 32nd notes for rolls
    static float rollTimer = 0.0f;
    rollTimer += static_cast<float>(ctx.dt());

    if (rollTimer >= rollInterval) {
        rollTimer -= rollInterval;
        for (int i = 0; i < 9; i++) {
            if (g_rollsRemaining[i] > 0) {
                drums[i]->trigger();
                g_drumDecay[i] = 0.7f;
                g_rollsRemaining[i]--;
                if (g_rollsRemaining[i] == 0) {
                    g_rollActive[i] = false;
                }
            }
        }
    }

    // Main clock trigger
    if (clock.triggered()) {
        // Flash current step
        g_stepFlash[g_currentStep] = 1.0f;

        // Trigger drums based on pattern
        for (int i = 0; i < 9; i++) {
            bool shouldHit = (g_patterns[i] >> g_currentStep) & 1;

            if (shouldHit) {
                // Check for skip
                if (randomFloat() < g_skipChance) {
                    continue;  // Skip this hit
                }

                // Check for roll
                if (randomFloat() < g_rollChance) {
                    g_rollActive[i] = true;
                    g_rollsRemaining[i] = g_rollCount;
                }

                // Trigger the drum
                drums[i]->trigger();
                g_drumDecay[i] = 1.0f;
            }
        }

        // Advance step
        g_currentStep = (g_currentStep + 1) % 16;
    }

    // =========================================================================
    // VISUAL UPDATE
    // =========================================================================

    // Decay visual states
    float decayRate = 1.0f - static_cast<float>(ctx.dt()) * 8.0f;
    float flashDecay = 1.0f - static_cast<float>(ctx.dt()) * 15.0f;

    for (int i = 0; i < 9; i++) {
        g_drumDecay[i] *= decayRate;
        if (g_drumDecay[i] < 0.01f) g_drumDecay[i] = 0.0f;
    }

    for (int i = 0; i < 16; i++) {
        g_stepFlash[i] *= flashDecay;
        if (g_stepFlash[i] < 0.01f) g_stepFlash[i] = 0.0f;
    }

    // Draw canvas
    canvas.clear(0.06f, 0.06f, 0.08f, 1.0f);

    // Draw title
    // (Canvas doesn't have text, so we skip title)

    // Draw sequencer grid
    float gridX = 350.0f;
    float gridY = 80.0f;
    float gridW = 880.0f;
    float gridH = 540.0f;

    drawSequencerGrid(canvas, gridX, gridY, gridW, gridH);

    // Draw drum name labels (left side bars)
    float labelW = 60.0f;
    float cellH = gridH / 9.0f;
    for (int i = 0; i < 9; i++) {
        ImU32 col = DRUM_COLORS[i];
        float r = ((col >> 0) & 0xFF) / 255.0f;
        float gr = ((col >> 8) & 0xFF) / 255.0f;
        float b = ((col >> 16) & 0xFF) / 255.0f;

        float barW = labelW * (0.3f + g_drumDecay[i] * 0.7f);
        float cy = gridY + i * cellH + 2;
        float ch = cellH - 4;

        canvas.fillStyle(r, gr, b, 0.8f);
        canvas.fillRect(gridX - labelW - 10, cy, barW, ch);
    }

    // Draw glitch indicator
    if (g_glitchAmount > 0.01f) {
        float glitchBar = g_glitchAmount * 200.0f;
        canvas.fillStyle(1.0f, 0.2f, 0.8f, 0.8f);
        canvas.fillRect(gridX + gridW + 20, gridY, 20, glitchBar);
    }
}

VIVID_CHAIN(setup, update)
