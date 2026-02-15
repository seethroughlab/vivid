/**
 * Modular Per-Voice Modulation Example
 *
 * Demonstrates: Modulator system, LFO, ADSRMod, per-voice vs global modulation
 *
 * This example shows the new modular modulation architecture where:
 * - Modulators can be attached to synths for per-voice control
 * - The same modulator classes work standalone OR attached
 * - Multiple modulators can target the same parameter (they sum)
 * - perVoice toggle switches between per-note and global behavior
 *
 * Key concepts:
 * - synth.addModulator<T>(name) - Attach a modulator to a synth
 * - synth.modulate(mod, param, depth) - Route modulator to parameter
 * - mod.perVoice = true - Each voice gets independent modulator state
 * - mod.retrigger = true - Reset modulator phase on noteOn
 */

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <vivid/audio/modulators/lfo.h>
#include <vivid/audio/modulators/adsr.h>

using namespace vivid;
using namespace vivid::audio;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // ==========================================================================
    // Wavetable Synth with attached modulators
    // ==========================================================================
    auto& synth = chain.add<WavetableSynth>("synth");
    synth.loadBuiltin(BuiltinTable::Analog);
    synth.maxVoices = 6;
    synth.volume = 0.6f;

    // Basic envelope
    synth.attack = 0.01f;
    synth.decay = 0.2f;
    synth.sustain = 0.7f;
    synth.release = 0.4f;

    // Unison for thickness
    synth.unisonVoices = 2;
    synth.unisonSpread = 10.0f;
    synth.unisonStereo = 0.7f;

    // Base filter settings (will be modulated)
    synth.filterCutoff = 800.0f;     // Low base cutoff
    synth.filterResonance = 0.4f;
    synth.setFilterType(SynthFilterType::LP24);

    // ==========================================================================
    // Per-Voice Filter Envelope (ADSRMod)
    // Each note gets its own envelope - classic subtractive synth sound
    // ==========================================================================
    auto& filterEnv = synth.addModulator<ADSRMod>("filterEnv");
    filterEnv.attack = 0.005f;       // Fast attack for plucky sound
    filterEnv.decay = 0.4f;          // Medium decay
    filterEnv.sustain = 0.1f;        // Low sustain - most action in decay
    filterEnv.release = 0.3f;
    filterEnv.perVoice = true;       // IMPORTANT: Each voice has own envelope

    // Route envelope to filter cutoff with high depth
    // Depth of 0.8 means envelope sweeps 80% of parameter range
    synth.modulate(filterEnv, "filterCutoff", 0.8f, false);  // false = unipolar (0-1)

    // ==========================================================================
    // Per-Voice LFO for Wavetable Position
    // Creates evolving timbre that's unique per voice
    // ==========================================================================
    auto& posLfo = synth.addModulator<vivid::audio::LFO>("positionLfo");
    posLfo.rate = 0.3f;              // Slow drift
    posLfo.waveform = vivid::audio::LFOWaveform::Triangle;
    posLfo.perVoice = true;          // Each voice has independent phase
    posLfo.retrigger = true;         // Reset phase on new note

    // Route to wavetable position
    synth.modulate(posLfo, "position", 0.4f);  // Sweep through 40% of wavetable

    // ==========================================================================
    // Global LFO for Subtle Filter Movement
    // All voices share the same LFO phase - creates unified "breathing"
    // ==========================================================================
    auto& filterLfo = synth.addModulator<vivid::audio::LFO>("filterLfo");
    filterLfo.rate = 0.15f;          // Very slow
    filterLfo.waveform = vivid::audio::LFOWaveform::Sine;
    filterLfo.perVoice = false;      // Global - all voices share this

    // Also targets filterCutoff - sums with the envelope modulation
    synth.modulate(filterLfo, "filterCutoff", 0.15f);

    // ==========================================================================
    // Per-Voice Tremolo LFO
    // Creates rhythmic volume modulation per voice
    // ==========================================================================
    auto& tremolo = synth.addModulator<vivid::audio::LFO>("tremolo");
    tremolo.rate = 4.0f;             // Faster for tremolo effect
    tremolo.waveform = vivid::audio::LFOWaveform::Sine;
    tremolo.perVoice = true;
    tremolo.retrigger = false;       // Don't reset - creates phase variety

    // Route to volume with subtle depth
    synth.modulate(tremolo, "volume", 0.15f);

    // ==========================================================================
    // Effects chain
    // ==========================================================================
    auto& delay = chain.add<Delay>("delay");
    delay.input("synth");
    delay.delayTime = 375.0f;        // Dotted eighth feel (in ms)
    delay.feedback = 0.4f;
    delay.mix = 0.25f;

    auto& reverb = chain.add<Reverb>("reverb");
    reverb.input("delay");
    reverb.roomSize = 0.7f;
    reverb.mix = 0.3f;

    // ==========================================================================
    // Visual feedback
    // ==========================================================================
    auto& noise = chain.add<Noise>("visual");
    noise.scale = 4.0f;

    chain.output("visual");
    chain.audioOutput("reverb");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    auto& synth = chain.get<WavetableSynth>("synth");
    auto& noise = chain.get<Noise>("visual");

    // ==========================================================================
    // Play a chord progression
    // ==========================================================================
    static float lastChordTime = -10.0f;
    static int chordIndex = 0;

    // Chord progression: Am7 -> Dm7 -> G7 -> Cmaj7
    // Each chord is an array of frequencies
    static const float chords[][4] = {
        {220.00f, 261.63f, 329.63f, 392.00f},  // Am7: A3, C4, E4, G4
        {293.66f, 349.23f, 440.00f, 523.25f},  // Dm7: D4, F4, A4, C5
        {196.00f, 246.94f, 293.66f, 349.23f},  // G7:  G3, B3, D4, F4
        {261.63f, 329.63f, 392.00f, 493.88f},  // Cmaj7: C4, E4, G4, B4
    };

    float chordDuration = 2.0f;  // 2 seconds per chord

    if (t - lastChordTime >= chordDuration) {
        // Release previous chord
        if (lastChordTime > 0) {
            int prevChord = (chordIndex + 3) % 4;
            for (int i = 0; i < 4; i++) {
                synth.noteOff(chords[prevChord][i]);
            }
        }

        // Play new chord with slightly staggered timing for realism
        for (int i = 0; i < 4; i++) {
            // Slight velocity variation
            float vel = 0.7f + 0.2f * (float)(i % 2);
            synth.noteOn(chords[chordIndex][i], vel);
        }

        chordIndex = (chordIndex + 1) % 4;
        lastChordTime = t;
    }

    // ==========================================================================
    // Visual responds to synth state
    // ==========================================================================
    // Use active voice count for visual intensity
    int voices = synth.activeVoiceCount();
    noise.scale = 2.0f + voices * 0.5f;

    // Animate based on time within chord
    float chordPhase = (t - lastChordTime) / chordDuration;
    noise.speed = 0.2f + (1.0f - chordPhase) * 0.4f;  // Faster at chord start

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
