/**
 * Lo-Fi Textures Example
 *
 * Demonstrates: Crackle, TapeEffect, Bitcrush
 *
 * Creates vintage lo-fi audio textures with vinyl crackle,
 * tape saturation, and bit-reduction effects.
 */

#include <vivid/vivid.h>

using namespace vivid;
using namespace vivid::audio;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Simple pad sound source
    chain.add<WavetableSynth>("pad");
    auto& pad = chain.get<WavetableSynth>("pad");
    pad.loadBuiltin(BuiltinTable::Analog);
    pad.maxVoices = 4;
    pad.attack = 0.5f;
    pad.decay = 0.3f;
    pad.sustain = 0.7f;
    pad.release = 1.0f;
    pad.position = 0.3f;

    // Tape saturation and wow/flutter
    chain.add<TapeEffect>("tape");
    auto& tape = chain.get<TapeEffect>("tape");
    tape.setInput(&pad);
    tape.saturation = 0.4f;      // Gentle saturation
    tape.wowDepth = 0.002f;      // Subtle pitch wobble
    tape.wowRate = 0.5f;
    tape.flutterDepth = 0.001f;  // Fast micro-variations
    tape.flutterRate = 6.0f;
    tape.hissLevel = 0.05f;      // Background hiss

    // Bit crusher for digital lo-fi
    chain.add<Bitcrush>("crush");
    auto& crush = chain.get<Bitcrush>("crush");
    crush.setInput(&tape);
    crush.bits = 12;             // Reduce bit depth (16 → 12)
    crush.downsample = 2;        // Reduce sample rate
    crush.mix = 0.3f;            // Blend with clean

    // Vinyl crackle layer
    chain.add<Crackle>("crackle");
    auto& crackle = chain.get<Crackle>("crackle");
    crackle.density = 0.0005f;   // Sparse clicks
    crackle.volume = 0.08f;      // Subtle

    // Mix crackle with processed audio
    chain.add<AudioMixer>("mix");
    auto& mixer = chain.get<AudioMixer>("mix");
    mixer.addInput(&crush, 1.0f);
    mixer.addInput(&crackle, 1.0f);

    // Visual - grainy noise texture
    chain.add<Noise>("visual");
    auto& noise = chain.get<Noise>("visual");
    noise.scale = 100.0f;        // Fine grain
    noise.octaves = 1;

    // Add film grain overlay
    chain.add<FilmGrain>("grain");
    auto& grain = chain.get<FilmGrain>("grain");
    grain.setInput("visual");
    grain.intensity = 0.15f;
    grain.size = 1.5f;

    chain.output("grain");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    auto& pad = chain.get<WavetableSynth>("pad");
    auto& tape = chain.get<TapeEffect>("tape");
    auto& crush = chain.get<Bitcrush>("crush");
    auto& noise = chain.get<Noise>("visual");

    // Play a slow chord progression
    static float lastChord = -10.0f;
    static int chordIndex = 0;

    // Chord changes every 4 seconds
    if (t - lastChord > 4.0f) {
        pad.allNotesOff();

        // Simple chord voicings (Cmaj7, Am7, Fmaj7, G7)
        float chords[4][3] = {
            {261.63f, 329.63f, 493.88f},  // Cmaj7
            {220.00f, 261.63f, 392.00f},  // Am7
            {174.61f, 261.63f, 329.63f},  // Fmaj7
            {196.00f, 293.66f, 349.23f}   // G7
        };

        for (int i = 0; i < 3; i++) {
            pad.noteOn(chords[chordIndex][i], 0.6f);
        }

        chordIndex = (chordIndex + 1) % 4;
        lastChord = t;
    }

    // Modulate tape wobble with time
    tape.wowDepth = 0.002f + 0.001f * std::sin(t * 0.1f);

    // Vary bit reduction
    crush.bits = 10 + static_cast<int>(2.0f * std::sin(t * 0.3f));

    // Animate visual grain
    noise.offset.set(t * 10.0f, t * 10.0f);

    chain.process();
}

VIVID_CHAIN(setup, update)
