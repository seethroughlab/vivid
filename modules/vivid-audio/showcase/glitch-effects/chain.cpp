// Glitch Effects - Vivid Example
// Demonstrates: BeatRepeat, Reverse, Stutter, Scratch, TapeStop, FrequencyShift, Stretch, Glitch
//
// Tempo-synced audio manipulation effects for creative production

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <vivid/audio_output.h>
#include <cmath>
#include <cstring>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

// Track which effect is active
static int g_activeEffect = 0;
static const int NUM_EFFECTS = 8;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // ----- CLOCK -----
    auto& clock = chain.add<Clock>("clock");
    clock.bpm = 120.0f;
    clock.division(ClockDiv::Sixteenth);
    clock.start();

    // ----- DRUM SOURCE -----
    auto& kick = chain.add<Kick>("kick");
    kick.pitch = 55.0f;
    kick.click = 0.4f;
    kick.drive = 0.2f;
    kick.volume = 0.8f;

    auto& snare = chain.add<Snare>("snare");
    snare.pitch = 180.0f;
    snare.snappy = 0.5f;
    snare.noise = 0.6f;
    snare.volume = 0.5f;

    auto& hat = chain.add<HiHat>("hat");
    hat.tone = 0.7f;
    hat.decay = 0.08f;
    hat.volume = 0.3f;

    auto& drums = chain.add<AudioMixer>("drums");
    drums.setInput(0, "kick");
    drums.setInput(1, "snare");
    drums.setInput(2, "hat");
    drums.setGain(0, 1.0f);
    drums.setGain(1, 0.8f);
    drums.setGain(2, 0.6f);

    // ----- GLITCH EFFECTS -----
    // Each effect processes the drum mix

    // 1. BEAT REPEAT - Loop slices with decay
    auto& repeat = chain.add<BeatRepeat>("repeat");
    repeat.input("drums");
    repeat.bpm = 120.0f;
    repeat.triggerDiv(ClockDiv::Quarter);
    repeat.sliceDiv(ClockDiv::Sixteenth);
    repeat.repeatCount = 4;
    repeat.decay = 0.15f;
    repeat.chance = 1.0f;  // Always trigger for demo

    // 2. REVERSE - Play audio backwards
    auto& reverse = chain.add<Reverse>("reverse");
    reverse.input("drums");
    reverse.bpm = 120.0f;
    reverse.triggerDiv(ClockDiv::Half);
    reverse.reverseDiv(ClockDiv::Quarter);
    reverse.chance = 1.0f;

    // 3. STUTTER - Rapid repeats with envelope
    auto& stutter = chain.add<Stutter>("stutter");
    stutter.input("drums");
    stutter.bpm = 120.0f;
    stutter.triggerDiv(ClockDiv::Half);
    stutter.stutterDiv(ClockDiv::ThirtySecond);
    stutter.stutterCount = 8;
    stutter.envelope(StutterEnvelope::Build);
    stutter.envAmount = 0.7f;
    stutter.chance = 1.0f;

    // 4. SCRATCH - DJ-style varispeed
    auto& scratch = chain.add<Scratch>("scratch");
    scratch.input("drums");
    scratch.bpm = 120.0f;
    scratch.triggerDiv(ClockDiv::Half);
    scratch.motion(ScratchMotion::BackForth);
    scratch.speed = 1.2f;
    scratch.speedRandom = 0.3f;
    scratch.scratchBeats = 0.5f;
    scratch.chance = 1.0f;

    // 5. TAPE STOP - Turntable slowdown
    auto& tape = chain.add<TapeStop>("tape");
    tape.input("drums");
    tape.bpm = 120.0f;
    tape.triggerDiv(ClockDiv::Whole);
    tape.mode(TapeMode::StopStart);
    tape.stopTime = 400.0f;
    tape.startTime = 200.0f;
    tape.chance = 1.0f;

    // 6. FREQUENCY SHIFT - Bode shifter
    auto& freq = chain.add<FrequencyShift>("freq");
    freq.input("drums");
    freq.shift = 30.0f;  // 30 Hz shift
    freq.bpm = 120.0f;
    freq.modDiv(ClockDiv::Quarter);
    freq.modDepth = 20.0f;
    freq.mix = 0.7f;

    // 7. STRETCH - Granular time-stretch
    auto& stretch = chain.add<Stretch>("stretch");
    stretch.input("drums");
    stretch.bpm = 120.0f;
    stretch.triggerDiv(ClockDiv::Whole);
    stretch.stretchDiv(ClockDiv::Quarter);
    stretch.stretchFactor = 2.0f;
    stretch.grainSize = 60.0f;
    stretch.overlap = 0.5f;
    stretch.chance = 1.0f;

    // 8. GLITCH - Meta-effect with all combined
    auto& glitch = chain.add<Glitch>("glitch");
    glitch.input("drums");
    glitch.bpm = 120.0f;
    glitch.triggerDiv(ClockDiv::Quarter);
    glitch.repeatChance = 0.2f;
    glitch.reverseChance = 0.15f;
    glitch.stutterChance = 0.15f;
    glitch.scratchChance = 0.1f;
    glitch.tapeChance = 0.08f;
    glitch.shiftChance = 0.1f;

    // ----- EFFECT SELECTOR -----
    auto& mixer = chain.add<AudioMixer>("effectMixer");
    mixer.setInput(0, "repeat");
    mixer.setInput(1, "reverse");
    mixer.setInput(2, "stutter");
    mixer.setInput(3, "scratch");
    mixer.setInput(4, "tape");
    mixer.setInput(5, "freq");
    mixer.setInput(6, "stretch");
    mixer.setInput(7, "glitch");
    for (int i = 0; i < NUM_EFFECTS; i++) {
        mixer.setGain(i, 0.0f);
    }

    // ----- LEVELS ANALYZER -----
    auto& levels = chain.add<Levels>("levels");
    levels.input("effectMixer");
    levels.smoothing = 0.8f;

    // ----- AUDIO OUTPUT -----
    auto& output = chain.add<AudioOutput>("audio_out");
    output.setInput("effectMixer");
    chain.audioOutput("audio_out");

    // ----- VISUALS -----
    auto& canvas = chain.add<Canvas>("canvas");
    canvas.size(1280, 720);
    canvas.loadBuiltinFont(ctx, BuiltinFont::Mono, 14.0f);

    chain.output("canvas");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    auto& clock = chain.get<Clock>("clock");
    auto& kick = chain.get<Kick>("kick");
    auto& snare = chain.get<Snare>("snare");
    auto& hat = chain.get<HiHat>("hat");
    auto& mixer = chain.get<AudioMixer>("effectMixer");

    // Drum pattern
    static int step = 0;
    if (clock.triggered()) {
        int beat = step % 16;

        // Kick: four-on-the-floor with variation
        if (beat == 0 || beat == 4 || beat == 8 || beat == 12) {
            kick.trigger();
        }

        // Snare: backbeat
        if (beat == 4 || beat == 12) {
            snare.trigger();
        }

        // Hi-hat: every other 16th
        if (beat % 2 == 0) {
            hat.decay = (beat % 4 == 2) ? 0.15f : 0.06f;
            hat.trigger();
        }

        step++;
    }

    // Mouse X: select effect
    float mouseX = ctx.mouseNorm().x;
    g_activeEffect = static_cast<int>(mouseX * NUM_EFFECTS);
    if (g_activeEffect >= NUM_EFFECTS) g_activeEffect = NUM_EFFECTS - 1;

    // Set mixer gains
    for (int i = 0; i < NUM_EFFECTS; i++) {
        mixer.setGain(i, (i == g_activeEffect) ? 1.0f : 0.0f);
    }

    // Mouse Y: adjust main parameter per effect
    float mouseY = ctx.mouseNorm().y;

    switch (g_activeEffect) {
        case 0: {  // BeatRepeat
            auto& repeat = chain.get<BeatRepeat>("repeat");
            repeat.repeatCount = 2 + static_cast<int>(mouseY * 6);
            break;
        }
        case 1: {  // Reverse
            auto& reverse = chain.get<Reverse>("reverse");
            // Y controls mix
            reverse.mix = mouseY;
            break;
        }
        case 2: {  // Stutter
            auto& stutter = chain.get<Stutter>("stutter");
            stutter.stutterCount = 4 + static_cast<int>(mouseY * 12);
            break;
        }
        case 3: {  // Scratch
            auto& scratch = chain.get<Scratch>("scratch");
            scratch.speed = 0.5f + mouseY * 1.5f;
            break;
        }
        case 4: {  // TapeStop
            auto& tape = chain.get<TapeStop>("tape");
            tape.stopTime = 100.0f + mouseY * 700.0f;
            break;
        }
        case 5: {  // FrequencyShift
            auto& freq = chain.get<FrequencyShift>("freq");
            freq.shift = -100.0f + mouseY * 200.0f;
            break;
        }
        case 6: {  // Stretch
            auto& stretch = chain.get<Stretch>("stretch");
            stretch.stretchFactor = 0.5f + mouseY * 3.5f;
            break;
        }
        case 7: {  // Glitch
            auto& glitch = chain.get<Glitch>("glitch");
            // Y scales all chances
            float scale = 0.5f + mouseY;
            glitch.repeatChance = 0.2f * scale;
            glitch.reverseChance = 0.15f * scale;
            glitch.stutterChance = 0.15f * scale;
            break;
        }
    }

    // ----- CANVAS VISUALIZATION -----
    static const char* effectNames[] = {
        "BEAT REPEAT", "REVERSE", "STUTTER", "SCRATCH",
        "TAPE STOP", "FREQ SHIFT", "STRETCH", "GLITCH"
    };

    auto& canvas = chain.get<Canvas>("canvas");
    auto& levels = chain.get<Levels>("levels");

    // Dark background
    canvas.clear(0.06f, 0.04f, 0.08f, 1.0f);

    // Draw effect name
    canvas.fillStyle(1.0f, 1.0f, 1.0f, 1.0f);
    canvas.textAlign(TextAlign::Left);
    canvas.textBaseline(TextBaseline::Top);
    float nameX = 640.0f - strlen(effectNames[g_activeEffect]) * 4.2f;
    canvas.fillText(effectNames[g_activeEffect], nameX, 40.0f);

    // Draw parameter info
    canvas.fillStyle(0.7f, 0.7f, 0.7f, 1.0f);
    char paramBuf[64];
    switch (g_activeEffect) {
        case 0: snprintf(paramBuf, sizeof(paramBuf), "Repeats: %d", 2 + (int)(mouseY * 6)); break;
        case 1: snprintf(paramBuf, sizeof(paramBuf), "Mix: %d%%", (int)(mouseY * 100)); break;
        case 2: snprintf(paramBuf, sizeof(paramBuf), "Stutters: %d", 4 + (int)(mouseY * 12)); break;
        case 3: snprintf(paramBuf, sizeof(paramBuf), "Speed: %.2fx", 0.5f + mouseY * 1.5f); break;
        case 4: snprintf(paramBuf, sizeof(paramBuf), "Stop Time: %.0fms", 100.0f + mouseY * 700.0f); break;
        case 5: snprintf(paramBuf, sizeof(paramBuf), "Shift: %.0f Hz", -100.0f + mouseY * 200.0f); break;
        case 6: snprintf(paramBuf, sizeof(paramBuf), "Factor: %.2fx", 0.5f + mouseY * 3.5f); break;
        case 7: snprintf(paramBuf, sizeof(paramBuf), "Intensity: %d%%", (int)((0.5f + mouseY) * 100)); break;
    }
    float paramX = 640.0f - strlen(paramBuf) * 4.2f;
    canvas.fillText(paramBuf, paramX, 60.0f);

    // Draw audio level visualization
    float rms = levels.rms();
    float peak = levels.peak();

    // RMS bar
    float barY = 600.0f;
    float barWidth = 800.0f;
    float barHeight = 30.0f;

    canvas.fillStyle(0.2f, 0.2f, 0.25f, 1.0f);
    canvas.fillRect(240.0f, barY, barWidth, barHeight);

    // RMS level (green)
    float rmsWidth = rms * barWidth;
    canvas.fillStyle(0.2f, 0.8f, 0.4f, 0.9f);
    canvas.fillRect(240.0f, barY, rmsWidth, barHeight);

    // Peak indicator (white line)
    float peakX = 240.0f + peak * barWidth;
    canvas.fillStyle(1.0f, 1.0f, 1.0f, 0.8f);
    canvas.fillRect(peakX - 1.0f, barY, 2.0f, barHeight);

    // Effect indicator circles
    float circleY = 150.0f;
    float spacing = 140.0f;
    float startX = 640.0f - (NUM_EFFECTS - 1) * spacing * 0.5f;

    for (int i = 0; i < NUM_EFFECTS; i++) {
        float cx = startX + i * spacing;
        float radius = (i == g_activeEffect) ? 25.0f : 15.0f;

        // Pulsing effect for active
        if (i == g_activeEffect) {
            radius += rms * 10.0f;
        }

        // Color based on effect type
        float hue = static_cast<float>(i) / NUM_EFFECTS;
        canvas.fillStyle(
            0.5f + 0.5f * std::sin(hue * 6.28f),
            0.5f + 0.5f * std::sin(hue * 6.28f + 2.09f),
            0.5f + 0.5f * std::sin(hue * 6.28f + 4.19f),
            (i == g_activeEffect) ? 1.0f : 0.4f
        );

        // Draw circle as rect approximation
        canvas.fillRect(cx - radius, circleY - radius, radius * 2, radius * 2);
    }

    // Control hints
    canvas.fillStyle(0.4f, 0.4f, 0.4f, 1.0f);
    canvas.textBaseline(TextBaseline::Bottom);
    canvas.fillText("Mouse X: Select Effect    Mouse Y: Adjust Parameter", 40.0f, 700.0f);
}

VIVID_CHAIN(setup, update)
