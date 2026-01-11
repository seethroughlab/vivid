// Glitch Demo - All Effects Complete
// Glitch meta-effect (6 effects) + Stretch (granular time-stretch)

#include <vivid/vivid.h>
#include <vivid/audio/audio.h>
#include <vivid/audio_output.h>
#include <vivid/effects/gradient.h>

using namespace vivid;
using namespace vivid::audio;
using namespace vivid::effects;

Clock* clk;
Kick* kick;
Snare* snare;
HiHat* hat;
AudioMixer* drums;
Glitch* glitch;
Stretch* stretch;
Levels* levels;
Gradient* grad;

int step = 0;

void setup(Context& ctx) {
    // === CLOCK (120 BPM) ===
    clk = &ctx.chain().add<Clock>("clk");
    clk->bpm = 120.0f;
    clk->division(ClockDiv::Sixteenth);

    // === DRUMS ===
    kick = &ctx.chain().add<Kick>("kick");
    kick->pitch = 55.0f;
    kick->click = 0.4f;
    kick->pitchDecay = 0.15f;
    kick->drive = 0.2f;
    kick->volume = 0.7f;

    snare = &ctx.chain().add<Snare>("snare");
    snare->pitch = 180.0f;
    snare->snappy = 0.5f;
    snare->noise = 0.6f;
    snare->noiseDecay = 0.2f;
    snare->volume = 0.5f;

    hat = &ctx.chain().add<HiHat>("hat");
    hat->tone = 0.7f;
    hat->decay = 0.08f;
    hat->volume = 0.25f;

    drums = &ctx.chain().add<AudioMixer>("drums");
    drums->setInput(0, "kick");
    drums->setInput(1, "snare");
    drums->setInput(2, "hat");
    drums->setGain(0, 1.0f);
    drums->setGain(1, 0.8f);
    drums->setGain(2, 0.6f);

    // === GLITCH META-EFFECT ===
    // Single operator replaces the 6 chained effects!
    glitch = &ctx.chain().add<Glitch>("glitch");
    glitch->input("drums");
    glitch->bpm = 120.0f;
    glitch->triggerDiv(ClockDiv::Quarter);

    // Set per-effect probabilities
    glitch->repeatChance = 0.2f;    // BeatRepeat
    glitch->reverseChance = 0.15f;  // Reverse
    glitch->stutterChance = 0.15f;  // Stutter
    glitch->scratchChance = 0.1f;   // Scratch
    glitch->tapeChance = 0.08f;     // TapeStop
    glitch->shiftChance = 0.1f;     // FrequencyShift
    glitch->mix = 1.0f;

    // === STRETCH (Granular Time-Stretch) ===
    stretch = &ctx.chain().add<Stretch>("stretch");
    stretch->input("glitch");
    stretch->bpm = 120.0f;
    stretch->triggerDiv(ClockDiv::Whole);     // Check every bar
    stretch->stretchDiv(ClockDiv::Quarter);   // Stretch a quarter note
    stretch->stretchFactor = 2.0f;            // Double the time (half speed)
    stretch->grainSize = 60.0f;               // 60ms grains
    stretch->overlap = 0.5f;
    stretch->chance = 0.15f;                  // 15% chance

    // === OUTPUT ===
    auto& out = ctx.chain().add<AudioOutput>("out");
    out.setInput("stretch");
    ctx.chain().audioOutput("out");

    // === VISUALS ===
    levels = &ctx.chain().add<Levels>("levels");
    levels->input("stretch");
    levels->smoothing = 0.8f;

    grad = &ctx.chain().add<Gradient>("grad");
    grad->mode = GradientMode::Radial;
    grad->scale = 1.0f;
    grad->colorA.set(0.08f, 0.04f, 0.02f);
    grad->colorB.set(0.02f, 0.02f, 0.04f);

    ctx.chain().output("grad");
}

void update(Context& ctx) {
    float t = ctx.time();

    // Drum pattern
    if (clk->triggered()) {
        int beat = step % 16;

        // Kick: syncopated
        if (beat == 0 || beat == 6 || beat == 10) {
            kick->trigger();
        }

        // Snare: backbeat
        if (beat == 4 || beat == 12) {
            snare->trigger();
        }

        // Hi-hat: groove
        if (beat % 2 == 0) {
            hat->decay = (beat % 4 == 2) ? 0.15f : 0.06f;
            hat->trigger();
        }

        step++;
    }

    // === Reactive visuals ===
    float rms = levels->rms();

    float pulse = rms * 0.6f;
    grad->colorA.set(0.08f + pulse, 0.04f + pulse * 0.5f, 0.02f);
    grad->scale = 1.0f + rms * 0.5f;

    float drift = std::sin(t * 0.1f) * 0.02f;
    grad->colorB.set(0.02f + drift, 0.02f, 0.04f - drift);

    ctx.chain().process(ctx);
}

VIVID_CHAIN(setup, update)
