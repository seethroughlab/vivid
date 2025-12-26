// Moody Pads - Trip-hop / Downtempo Wavetable Synth Demo
// Showcases: unison, sub oscillator, filter envelope, portamento

#include <vivid/vivid.h>
#include <vivid/audio/audio.h>
#include <vivid/audio/notes.h>
#include <vivid/audio_output.h>

using namespace vivid;
using namespace vivid::audio;

WavetableSynth* pad;
WavetableSynth* bass;
Clock* clk;
int step = 0;

// Moody minor chord progression (Am - F - C - G)
const int chordRoot[] = { 57, 53, 48, 55 };  // A3, F3, C3, G3
const int chordType[] = { 0, 0, 0, 0 };      // 0 = minor, 1 = major

// Bass pattern
const int bassNotes[] = { 45, 45, 41, 41, 36, 36, 43, 43 };  // A2, F2, C2, G2

void setup(Context& ctx) {
    // === PAD SYNTH (lush, evolving) ===
    pad = &ctx.chain().add<WavetableSynth>("pad");
    pad->loadBuiltin(BuiltinTable::Analog);

    // Thick unison for width
    pad->unisonVoices = 5;
    pad->unisonSpread = 18.0f;
    pad->unisonStereo = 0.9f;

    // Sub for warmth
    pad->subLevel = 0.15f;
    pad->subOctave = -1;

    // Slow, dreamy envelope
    pad->attack = 0.8f;
    pad->decay = 0.5f;
    pad->sustain = 0.7f;
    pad->release = 2.0f;
    pad->volume = 0.25f;

    // Gentle filter - opens slowly
    pad->setFilterType(SynthFilterType::LP24);
    pad->filterCutoff = 600.0f;
    pad->filterResonance = 0.25f;
    pad->filterKeytrack = 0.3f;

    // Filter envelope - slow open/close
    pad->filterAttack = 1.2f;
    pad->filterDecay = 0.8f;
    pad->filterSustain = 0.4f;
    pad->filterRelease = 1.5f;
    pad->filterEnvAmount = 0.5f;

    // Smooth portamento between chords
    pad->portamento = 150.0f;

    // === BASS SYNTH (deep, subby) ===
    bass = &ctx.chain().add<WavetableSynth>("bass");
    bass->loadBuiltin(BuiltinTable::Basic);
    bass->position = 0.7f;  // Saw-ish

    bass->unisonVoices = 2;
    bass->unisonSpread = 8.0f;
    bass->unisonStereo = 0.3f;

    bass->subLevel = 0.5f;
    bass->subOctave = -1;

    bass->attack = 0.01f;
    bass->decay = 0.3f;
    bass->sustain = 0.4f;
    bass->release = 0.4f;
    bass->volume = 0.3f;

    // Plucky filter envelope
    bass->setFilterType(SynthFilterType::LP24);
    bass->filterCutoff = 400.0f;
    bass->filterResonance = 0.35f;
    bass->filterAttack = 0.001f;
    bass->filterDecay = 0.25f;
    bass->filterSustain = 0.1f;
    bass->filterRelease = 0.2f;
    bass->filterEnvAmount = 0.7f;

    // === CLOCK (slow trip-hop tempo ~85 BPM) ===
    clk = &ctx.chain().add<Clock>("clk");
    clk->bpm = 85.0f;
    clk->division(ClockDiv::Eighth);

    // === MIXER (combine synths) ===
    auto& mixer = ctx.chain().add<AudioMixer>("mixer");
    mixer.setInput(0, "pad");
    mixer.setInput(1, "bass");
    mixer.setGain(0, 1.0f);
    mixer.setGain(1, 1.0f);

    auto& out = ctx.chain().add<AudioOutput>("out");
    out.setInput("mixer");
    ctx.chain().audioOutput("out");
}

void update(Context& ctx) {
    // Trigger on 8th notes
    if (clk->triggered()) {
        step++;

        // Bass plays on every 8th note
        int bassStep = step % 8;
        int chordIdx = (step / 8) % 4;

        // Release previous bass note
        bass->allNotesOff();

        // Play new bass note
        int bassNote = bassNotes[bassStep] + (chordIdx == 0 ? 0 :
                       chordIdx == 1 ? -4 : chordIdx == 2 ? -9 : -2);
        bass->noteOnMidi(bassNote, 90 + (bassStep % 2) * 15);

        // Pad chord changes every 2 beats (8 8th notes per chord)
        if (bassStep == 0) {
            pad->allNotesOff();

            // Build minor chord
            int root = chordRoot[chordIdx];
            pad->noteOnMidi(root, 70);
            pad->noteOnMidi(root + 3, 65);   // Minor third
            pad->noteOnMidi(root + 7, 60);   // Fifth
            pad->noteOnMidi(root + 12, 55);  // Octave
        }
    }

    // Slowly evolve pad timbre
    float t = ctx.time();
    pad->position = 0.3f + 0.25f * std::sin(t * 0.15f);
    pad->filterCutoff = 500.0f + 300.0f * std::sin(t * 0.1f);

    // Subtle warp modulation
    pad->setWarpMode(WarpMode::FM);
    pad->warpAmount = 0.1f + 0.08f * std::sin(t * 0.2f);

    ctx.chain().process(ctx);
}

VIVID_CHAIN(setup, update)
