// Dark Ambient - Atmospheric Wavetable Drones
// Showcases: deep unison, slow filter evolution, vocal formants

#include <vivid/vivid.h>
#include <vivid/audio/audio.h>
#include <vivid/audio/notes.h>
#include <vivid/audio_output.h>

using namespace vivid;
using namespace vivid::audio;

WavetableSynth* drone1;
WavetableSynth* drone2;
WavetableSynth* shimmer;
int frame = 0;

void setup(Context& ctx) {
    // === DRONE 1 (deep, cavernous) ===
    drone1 = &ctx.chain().add<WavetableSynth>("drone1");
    drone1->loadBuiltin(BuiltinTable::Vocal);  // Formant-based for eerie quality

    drone1->unisonVoices = 6;
    drone1->unisonSpread = 12.0f;
    drone1->unisonStereo = 0.7f;

    drone1->subLevel = 0.4f;
    drone1->subOctave = -2;  // Two octaves down for deep rumble

    // Very slow envelope
    drone1->attack = 4.0f;
    drone1->decay = 2.0f;
    drone1->sustain = 0.9f;
    drone1->release = 6.0f;
    drone1->volume = 0.2f;

    // Dark, muffled filter
    drone1->setFilterType(SynthFilterType::LP24);
    drone1->filterCutoff = 400.0f;
    drone1->filterResonance = 0.15f;

    drone1->filterAttack = 5.0f;
    drone1->filterDecay = 3.0f;
    drone1->filterSustain = 0.3f;
    drone1->filterRelease = 4.0f;
    drone1->filterEnvAmount = 0.4f;

    // Subtle bend for organic movement
    drone1->setWarpMode(WarpMode::BendMinus);
    drone1->warpAmount = 0.2f;

    // === DRONE 2 (mid-range, hollow) ===
    drone2 = &ctx.chain().add<WavetableSynth>("drone2");
    drone2->loadBuiltin(BuiltinTable::Analog);

    drone2->unisonVoices = 4;
    drone2->unisonSpread = 20.0f;
    drone2->unisonStereo = 0.9f;

    drone2->attack = 5.0f;
    drone2->decay = 3.0f;
    drone2->sustain = 0.7f;
    drone2->release = 8.0f;
    drone2->volume = 0.15f;

    drone2->setFilterType(SynthFilterType::BP);
    drone2->filterCutoff = 800.0f;
    drone2->filterResonance = 0.4f;
    drone2->filterKeytrack = 0.5f;

    drone2->filterAttack = 6.0f;
    drone2->filterDecay = 4.0f;
    drone2->filterSustain = 0.5f;
    drone2->filterRelease = 5.0f;
    drone2->filterEnvAmount = 0.3f;

    // Mirror warp for hollow, spectral quality
    drone2->setWarpMode(WarpMode::Mirror);
    drone2->warpAmount = 0.3f;

    // Slow portamento for drifting
    drone2->portamento = 2000.0f;

    // === SHIMMER (high, crystalline) ===
    shimmer = &ctx.chain().add<WavetableSynth>("shimmer");
    shimmer->loadBuiltin(BuiltinTable::Digital);

    shimmer->unisonVoices = 8;
    shimmer->unisonSpread = 35.0f;
    shimmer->unisonStereo = 1.0f;

    shimmer->attack = 3.0f;
    shimmer->decay = 2.0f;
    shimmer->sustain = 0.6f;
    shimmer->release = 10.0f;
    shimmer->volume = 0.08f;

    shimmer->setFilterType(SynthFilterType::HP12);
    shimmer->filterCutoff = 2000.0f;
    shimmer->filterResonance = 0.3f;

    shimmer->filterAttack = 4.0f;
    shimmer->filterDecay = 3.0f;
    shimmer->filterSustain = 0.4f;
    shimmer->filterRelease = 6.0f;
    shimmer->filterEnvAmount = 0.5f;

    // FM warp for bell-like overtones
    shimmer->setWarpMode(WarpMode::FM);
    shimmer->warpAmount = 0.25f;

    shimmer->portamento = 3000.0f;

    // Start initial drones (D minor cluster)
    drone1->noteOnMidi(26, 60);   // D1
    drone1->noteOnMidi(33, 55);   // A1

    drone2->noteOnMidi(50, 50);   // D3
    drone2->noteOnMidi(53, 45);   // F3

    shimmer->noteOnMidi(74, 40);  // D5
    shimmer->noteOnMidi(77, 35);  // F5

    // === MIXER (combine drones) ===
    auto& mixer = ctx.chain().add<AudioMixer>("mixer");
    mixer.setInput(0, "drone1");
    mixer.setInput(1, "drone2");
    mixer.setInput(2, "shimmer");
    mixer.setGain(0, 1.0f);
    mixer.setGain(1, 1.0f);
    mixer.setGain(2, 1.0f);

    auto& out = ctx.chain().add<AudioOutput>("out");
    out.setInput("mixer");
    ctx.chain().audioOutput("out");
}

void update(Context& ctx) {
    frame++;
    float t = ctx.time();

    // Very slow chord changes (every ~30 seconds)
    int chordPhase = static_cast<int>(t / 30.0f) % 4;
    static int lastChord = -1;

    if (chordPhase != lastChord) {
        lastChord = chordPhase;

        drone1->allNotesOff();
        drone2->allNotesOff();
        shimmer->allNotesOff();

        switch (chordPhase) {
            case 0:  // Dm
                drone1->noteOnMidi(26, 60);
                drone1->noteOnMidi(33, 55);
                drone2->noteOnMidi(50, 50);
                drone2->noteOnMidi(53, 45);
                shimmer->noteOnMidi(74, 40);
                break;
            case 1:  // Bbmaj7
                drone1->noteOnMidi(22, 60);
                drone1->noteOnMidi(29, 55);
                drone2->noteOnMidi(46, 50);
                drone2->noteOnMidi(50, 45);
                shimmer->noteOnMidi(70, 40);
                shimmer->noteOnMidi(77, 35);
                break;
            case 2:  // Gm
                drone1->noteOnMidi(31, 60);
                drone1->noteOnMidi(26, 55);
                drone2->noteOnMidi(55, 50);
                drone2->noteOnMidi(50, 45);
                shimmer->noteOnMidi(79, 40);
                break;
            case 3:  // A (tension)
                drone1->noteOnMidi(21, 60);
                drone1->noteOnMidi(28, 55);
                drone2->noteOnMidi(52, 50);
                drone2->noteOnMidi(49, 45);
                shimmer->noteOnMidi(76, 40);
                shimmer->noteOnMidi(73, 35);
                break;
        }
    }

    // Glacial parameter evolution
    drone1->position = 0.2f + 0.3f * std::sin(t * 0.03f);
    drone1->filterCutoff = 300.0f + 200.0f * std::sin(t * 0.02f);
    drone1->warpAmount = 0.15f + 0.1f * std::sin(t * 0.05f);

    drone2->position = 0.4f + 0.3f * std::sin(t * 0.025f + 1.0f);
    drone2->filterCutoff = 600.0f + 400.0f * std::sin(t * 0.018f);
    drone2->warpAmount = 0.2f + 0.15f * std::sin(t * 0.04f);

    shimmer->position = 0.3f + 0.4f * std::sin(t * 0.035f + 2.0f);
    shimmer->filterCutoff = 1800.0f + 800.0f * std::sin(t * 0.022f);
    shimmer->warpAmount = 0.2f + 0.1f * std::sin(t * 0.06f);

    ctx.chain().process(ctx);
}

VIVID_CHAIN(setup, update)
