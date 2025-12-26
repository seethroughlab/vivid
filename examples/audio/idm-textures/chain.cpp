// IDM Textures - Glitchy Wavetable Exploration
// Showcases: warp modes, quantize, FM self-mod, filter sweeps

#include <vivid/vivid.h>
#include <vivid/audio/audio.h>
#include <vivid/audio/notes.h>
#include <vivid/audio_output.h>
#include <cstdlib>

using namespace vivid;
using namespace vivid::audio;

WavetableSynth* lead;
WavetableSynth* texture;
Clock* clk;
int step = 0;
uint32_t seed = 12345;

// Simple pseudo-random
float randf() {
    seed = seed * 1103515245 + 12345;
    return static_cast<float>(seed & 0x7FFFFFFF) / static_cast<float>(0x7FFFFFFF);
}

// Glitchy note sequence
const int notes[] = { 60, 63, 67, 60, 65, 63, 72, 67, 60, 58, 63, 65 };
const int numNotes = 12;

void setup(Context& ctx) {
    // === LEAD (glitchy, warp-heavy) ===
    lead = &ctx.chain().add<WavetableSynth>("lead");
    lead->loadBuiltin(BuiltinTable::Digital);

    lead->unisonVoices = 2;
    lead->unisonSpread = 25.0f;
    lead->unisonStereo = 0.8f;

    // Sharp, percussive envelope
    lead->attack = 0.001f;
    lead->decay = 0.15f;
    lead->sustain = 0.3f;
    lead->release = 0.2f;
    lead->volume = 0.28f;

    // Velocity affects brightness
    lead->velToVolume = 0.4f;
    lead->velToAttack = 0.3f;

    // Resonant filter for that IDM bite
    lead->setFilterType(SynthFilterType::LP24);
    lead->filterCutoff = 2000.0f;
    lead->filterResonance = 0.6f;
    lead->filterKeytrack = 0.7f;

    lead->filterAttack = 0.001f;
    lead->filterDecay = 0.1f;
    lead->filterSustain = 0.2f;
    lead->filterRelease = 0.15f;
    lead->filterEnvAmount = 0.6f;

    // Start with quantize warp for lo-fi grit
    lead->setWarpMode(WarpMode::Quantize);
    lead->warpAmount = 0.4f;

    // === TEXTURE (atmospheric drone bed) ===
    texture = &ctx.chain().add<WavetableSynth>("texture");
    texture->loadBuiltin(BuiltinTable::Texture);

    texture->unisonVoices = 6;
    texture->unisonSpread = 30.0f;
    texture->unisonStereo = 1.0f;

    texture->attack = 2.0f;
    texture->decay = 1.0f;
    texture->sustain = 0.8f;
    texture->release = 3.0f;
    texture->volume = 0.15f;

    texture->setFilterType(SynthFilterType::LP12);
    texture->filterCutoff = 1500.0f;
    texture->filterResonance = 0.2f;

    texture->filterAttack = 3.0f;
    texture->filterDecay = 2.0f;
    texture->filterSustain = 0.5f;
    texture->filterRelease = 2.0f;
    texture->filterEnvAmount = 0.3f;

    // Subtle FM warp for movement
    texture->setWarpMode(WarpMode::FM);
    texture->warpAmount = 0.15f;

    // Start drone
    texture->noteOnMidi(36, 60);  // C2
    texture->noteOnMidi(43, 55);  // G2

    // === CLOCK (IDM tempo ~130 BPM with swing) ===
    clk = &ctx.chain().add<Clock>("clk");
    clk->bpm = 128.0f;
    clk->division(ClockDiv::Sixteenth);

    // === MIXER (combine synths) ===
    auto& mixer = ctx.chain().add<AudioMixer>("mixer");
    mixer.setInput(0, "lead");
    mixer.setInput(1, "texture");
    mixer.setGain(0, 1.0f);
    mixer.setGain(1, 1.0f);

    auto& out = ctx.chain().add<AudioOutput>("out");
    out.setInput("mixer");
    ctx.chain().audioOutput("out");
}

void update(Context& ctx) {
    float t = ctx.time();

    // Trigger on 16th notes for glitchy patterns
    if (clk->triggered()) {
        step++;

        // Probabilistic note triggering (skip some steps)
        if (randf() > 0.3f) {
            lead->allNotesOff();

            int noteIdx = step % numNotes;
            int transpose = (step / 24) % 3 == 0 ? -12 : 0;  // Octave drops
            int note = notes[noteIdx] + transpose;

            // Varying velocity for dynamics
            int vel = 60 + static_cast<int>(randf() * 60.0f);

            lead->noteOnMidi(note, vel);

            // Randomize warp mode occasionally
            if (randf() > 0.85f) {
                int warpChoice = static_cast<int>(randf() * 5.0f);
                switch (warpChoice) {
                    case 0: lead->setWarpMode(WarpMode::Quantize); break;
                    case 1: lead->setWarpMode(WarpMode::Sync); break;
                    case 2: lead->setWarpMode(WarpMode::FM); break;
                    case 3: lead->setWarpMode(WarpMode::BendPlus); break;
                    case 4: lead->setWarpMode(WarpMode::Mirror); break;
                }
            }
        }
    }

    // Evolving lead parameters
    lead->warpAmount = 0.2f + 0.4f * std::sin(t * 0.5f) * std::sin(t * 0.17f);
    lead->position = 0.3f + 0.5f * std::abs(std::sin(t * 0.3f));
    lead->filterCutoff = 1500.0f + 2000.0f * std::sin(t * 0.25f);

    // Slow texture evolution
    texture->position = 0.2f + 0.6f * std::sin(t * 0.05f);
    texture->filterCutoff = 800.0f + 700.0f * std::sin(t * 0.08f);
    texture->warpAmount = 0.1f + 0.15f * std::sin(t * 0.12f);

    ctx.chain().process(ctx);
}

VIVID_CHAIN(setup, update)
