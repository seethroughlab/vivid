/**
 * Wavetable Polyphonic Synthesis Example
 *
 * Demonstrates: WavetableSynth, PolySynth, Envelope
 *
 * Shows polyphonic wavetable synthesis with morphing timbres
 * and envelope-controlled modulation.
 */

#include <vivid/vivid.h>

using namespace vivid;
using namespace vivid::audio;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Wavetable synth with analog preset
    chain.add<WavetableSynth>("wt");
    auto& wt = chain.get<WavetableSynth>("wt");
    wt.loadBuiltin(BuiltinTable::Analog);
    wt.maxVoices = 6;
    wt.position = 0.0f;          // Start of wavetable

    // Envelope settings
    wt.attack = 0.05f;
    wt.decay = 0.2f;
    wt.sustain = 0.6f;
    wt.release = 0.5f;

    // Unison for thickness
    wt.unisonVoices = 3;
    wt.unisonSpread = 15.0f;     // Cents detune
    wt.unisonStereo = 0.8f;      // Stereo width

    // Filter
    wt.filterCutoff = 2000.0f;
    wt.filterResonance = 0.3f;
    wt.setFilterType(SynthFilterType::LP24);

    // Filter envelope modulation
    wt.filterAttack = 0.01f;
    wt.filterDecay = 0.3f;
    wt.filterSustain = 0.2f;
    wt.filterRelease = 0.4f;
    wt.filterEnvAmount = 0.6f;   // Positive = opens filter

    // LFO for position modulation
    chain.add<LFO>("lfo");
    auto& lfo = chain.get<LFO>("lfo");
    lfo.frequency = 0.2f;
    lfo.waveform = LFOWaveform::Sine;

    // Add subtle chorus
    chain.add<Chorus>("chorus");
    auto& chorus = chain.get<Chorus>("chorus");
    chorus.setInput(&wt);
    chorus.rate = 0.5f;
    chorus.depth = 0.3f;
    chorus.mix = 0.3f;

    // Reverb for space
    chain.add<Reverb>("verb");
    auto& verb = chain.get<Reverb>("verb");
    verb.setInput(&chorus);
    verb.roomSize = 0.6f;
    verb.wet = 0.25f;

    // Visual feedback
    chain.add<Noise>("visual");
    auto& noise = chain.get<Noise>("visual");
    noise.scale = 4.0f;

    chain.output("visual");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    auto& wt = chain.get<WavetableSynth>("wt");
    auto& lfo = chain.get<LFO>("lfo");
    auto& noise = chain.get<Noise>("visual");

    // Modulate wavetable position with LFO
    wt.position = 0.5f + 0.4f * lfo.value();

    // Play arpeggiated chord (simple pattern)
    static float lastNote = 0.0f;
    static int noteIndex = 0;

    // Notes: C4, E4, G4, B4 (Cmaj7 arpeggio)
    float notes[] = {261.63f, 329.63f, 392.00f, 493.88f};
    float noteInterval = 0.25f;  // 250ms between notes

    if (t - lastNote > noteInterval) {
        // Release previous note if any voices are playing
        if (wt.isPlaying()) {
            wt.noteOff(notes[(noteIndex + 3) % 4]);
        }

        // Play new note
        wt.noteOn(notes[noteIndex], 0.8f);

        noteIndex = (noteIndex + 1) % 4;
        lastNote = t;
    }

    // Visual responds to wavetable position
    noise.scale = 2.0f + wt.position * 6.0f;
    noise.speed = 0.3f + wt.position * 0.5f;

    chain.process();
}

VIVID_CHAIN(setup, update)
