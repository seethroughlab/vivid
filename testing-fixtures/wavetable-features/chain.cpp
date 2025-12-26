// WavetableSynth Phase 1+2+3 Features Test
// Tests: unison, sub oscillator, portamento, velocity, warp modes, filter

#include <vivid/vivid.h>
#include <vivid/audio/audio.h>
#include <vivid/audio_output.h>

using namespace vivid;
using namespace vivid::audio;

WavetableSynth* synth;
int frame = 0;

void setup(Context& ctx) {
    // Create wavetable synth with all Phase 1-3 features
    synth = &ctx.chain().add<WavetableSynth>("wt");

    // Load saw-rich wavetable for filter demo
    synth->loadBuiltin(BuiltinTable::Analog);

    // === UNISON SETTINGS ===
    synth->unisonVoices = 3;
    synth->unisonSpread = 12.0f;
    synth->unisonStereo = 0.6f;

    // === SUB OSCILLATOR ===
    synth->subLevel = 0.25f;
    synth->subOctave = -1;

    // === PORTAMENTO ===
    synth->portamento = 80.0f;

    // === VELOCITY SENSITIVITY ===
    synth->velToVolume = 0.3f;
    synth->velToAttack = 0.2f;

    // Amplitude envelope - pad-like
    synth->attack = 0.05f;
    synth->decay = 0.3f;
    synth->sustain = 0.6f;
    synth->release = 0.8f;
    synth->volume = 0.3f;

    // Wavetable position
    synth->position = 0.6f;

    // === FILTER (classic subtractive synthesis) ===
    synth->setFilterType(SynthFilterType::LP24);  // 24dB/oct low-pass
    synth->filterCutoff = 800.0f;            // Start with low cutoff
    synth->filterResonance = 0.4f;           // Moderate resonance
    synth->filterKeytrack = 0.5f;            // 50% keytracking

    // === FILTER ENVELOPE (classic "pluck" sweep) ===
    synth->filterAttack = 0.001f;            // Instant attack
    synth->filterDecay = 0.4f;               // Moderate decay
    synth->filterSustain = 0.2f;             // Low sustain
    synth->filterRelease = 0.3f;             // Quick release
    synth->filterEnvAmount = 0.8f;           // Strong envelope modulation

    // Output to speakers
    auto& out = ctx.chain().add<AudioOutput>("out");
    out.setInput("wt");
    ctx.chain().audioOutput("out");
}

void update(Context& ctx) {
    frame++;

    // Demo sequence: play notes to hear filter envelope sweeps
    int seq = frame % 240;  // 4 second loop at 60fps

    // Play a bass note
    if (seq == 10) {
        synth->noteOnMidi(36, 110);  // C2
    }
    if (seq == 50) {
        synth->noteOffMidi(36);
    }

    // Play a chord
    if (seq == 60) {
        synth->noteOnMidi(48, 100);  // C3
        synth->noteOnMidi(55, 90);   // G3
    }
    if (seq == 140) {
        synth->noteOffMidi(48);
        synth->noteOffMidi(55);
    }

    // Play a higher note
    if (seq == 150) {
        synth->noteOnMidi(60, 80);   // C4
    }
    if (seq == 200) {
        synth->noteOffMidi(60);
    }

    // Slowly sweep base cutoff for variety
    float t = static_cast<float>(frame) * 0.005f;
    synth->filterCutoff = 600.0f + 400.0f * std::sin(t);

    // Slowly morph wavetable position
    synth->position = 0.4f + 0.3f * std::sin(static_cast<float>(frame) * 0.01f);

    ctx.chain().process(ctx);
}

VIVID_CHAIN(setup, update)
