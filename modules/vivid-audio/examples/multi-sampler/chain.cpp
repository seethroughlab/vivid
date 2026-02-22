/**
 * Multi-Sampler Example
 *
 * Demonstrates: MultiSampler
 *
 * Full-featured multi-zone sampler with velocity layers, key zones,
 * and per-region tuning. Manual region setup and MIDI-triggered playback.
 */

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>

using namespace vivid;
using namespace vivid::audio;
using namespace vivid::effects;

// Simple chord progression for demo
struct Chord {
    uint8_t notes[4];
    int count;
};

static const Chord chords[] = {
    {{60, 64, 67, 72}, 4},  // C major
    {{65, 69, 72, 77}, 4},  // F major
    {{67, 71, 74, 79}, 4},  // G major
    {{64, 67, 72, 76}, 4},  // E minor
};
static int chordIndex = 0;
static float lastChordTime = -10.0f;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Master clock
    auto& clock = chain.add<Clock>("clock");
    clock.bpm = 90.0f;
    clock.division(ClockDiv::Quarter);

    // =========================================================================
    // MULTISAMPLER: Multi-zone sampler with key zones and velocity layers
    // =========================================================================
    auto& multi = chain.add<MultiSampler>("instrument");
    multi.volume = 0.8f;
    multi.maxVoices = 16;

    // ADSR envelope (global, applies to all regions)
    multi.attack = 0.01f;
    multi.decay = 0.3f;
    multi.sustain = 0.7f;
    multi.release = 0.8f;

    // Velocity curve: 0 = linear, negative = soft, positive = hard
    multi.velCurve = 0.0f;

    // Define key zones manually using SampleRegion structs.
    // Each region maps a WAV file to a range of MIDI notes.

    // Low register zone (C2-B3)
    SampleRegion lowZone;
    lowZone.path = "assets/audio/samples/firered_0096.wav";
    lowZone.rootNote = 48;       // Original pitch: C3
    lowZone.loNote = 36;         // Responds from C2
    lowZone.hiNote = 59;         // Through B3
    lowZone.loVel = 0;           // All velocities
    lowZone.hiVel = 127;
    lowZone.volumeDb = 0.0f;
    multi.addRegion(lowZone);

    // Mid register zone (C4-B5)
    SampleRegion midZone;
    midZone.path = "assets/audio/samples/firered_00A3.wav";
    midZone.rootNote = 72;       // Original pitch: C5
    midZone.loNote = 60;         // Responds from C4
    midZone.hiNote = 83;         // Through B5
    midZone.loVel = 0;
    midZone.hiVel = 127;
    midZone.volumeDb = -2.0f;    // Slightly quieter
    multi.addRegion(midZone);

    // High register zone (C6-C8) - reuse mid sample pitched up
    SampleRegion highZone;
    highZone.path = "assets/audio/samples/firered_00A3.wav";
    highZone.rootNote = 72;
    highZone.loNote = 84;
    highZone.hiNote = 108;
    highZone.loVel = 0;
    highZone.hiVel = 127;
    highZone.volumeDb = -4.0f;   // Quieter for high range
    multi.addRegion(highZone);

    // Reverb for depth
    auto& reverb = chain.add<Reverb>("reverb");
    reverb.input("instrument");
    reverb.roomSize = 0.7f;
    reverb.damping = 0.3f;
    reverb.width = 1.0f;
    reverb.mix = 0.3f;

    // Audio output
    auto& out = chain.add<AudioOutput>("out");
    out.setInput("reverb");
    chain.audioOutput("out");

    // Visual output
    auto& visual = chain.add<Noise>("visual");
    visual.scale = 5.0f;
    visual.speed = 0.3f;
    chain.output("visual");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    auto& multi = chain.get<MultiSampler>("instrument");
    auto& visual = chain.get<Noise>("visual");

    // Play chord progression every 2 seconds
    if (t - lastChordTime >= 2.0f) {
        // Release previous chord
        if (chordIndex > 0 || lastChordTime > 0) {
            int prevIdx = (chordIndex + 3) % 4;  // Previous chord
            const auto& prev = chords[prevIdx];
            for (int i = 0; i < prev.count; i++) {
                multi.noteOff(prev.notes[i]);
            }
        }

        // Play new chord
        const auto& chord = chords[chordIndex];
        for (int i = 0; i < chord.count; i++) {
            float vel = 0.6f + 0.2f * (i == 0 ? 1.0f : 0.0f);  // Accent root
            multi.noteOn(chord.notes[i], vel);
        }

        chordIndex = (chordIndex + 1) % 4;
        lastChordTime = t;
        visual.scale = 3.0f;
    } else {
        visual.scale = static_cast<float>(visual.scale) +
                       (5.0f - static_cast<float>(visual.scale)) * 0.03f;
    }

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
