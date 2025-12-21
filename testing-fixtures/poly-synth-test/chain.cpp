// PolySynth test - verify polyphonic voice management
// Plays chord progressions with audio-reactive shapes and particles
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <vivid/audio/notes.h>
#include <vivid/audio_output.h>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;
using namespace vivid::audio::freq;

// Chord progression: Am - F - C - G (common minor progression)
struct ChordInfo {
    float notes[4];
    int count;
    int polygonSides;  // Visual representation
    float hue;         // Base color
};

static const ChordInfo chords[] = {
    {{A3, C4, E4, A4}, 4, 6, 0.00f},   // Am - hexagon, red
    {{F3, A3, C4, F4}, 4, 4, 0.15f},   // F  - square, orange
    {{C3, E3, G3, C4}, 4, 3, 0.55f},   // C  - triangle, cyan
    {{G3, B3, D4, G4}, 4, 5, 0.75f},   // G  - pentagon, purple
};
static const int numChords = sizeof(chords) / sizeof(chords[0]);
static int chordIndex = 0;
static int prevChordIndex = -1;
static float chordTime = 0.0f;
static const float chordDuration = 2.0f;
static float chordEnvelope = 0.0f;  // Smooth envelope for transitions

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // Audio: PolySynth through tape
    // =========================================================================

    auto& synth = chain.add<PolySynth>("synth");
    synth.waveform(Waveform::Saw);
    synth.maxVoices = 8;
    synth.volume = 0.6f;
    synth.attack = 0.1f;
    synth.decay = 0.2f;
    synth.sustain = 0.7f;
    synth.release = 0.8f;
    synth.unisonDetune = 8.0f;

    auto& tape = chain.add<TapeEffect>("tape");
    tape.input("synth");
    tape.wow = 0.15f;
    tape.flutter = 0.1f;
    tape.saturation = 0.3f;
    tape.hiss = 0.05f;

    auto& audioOut = chain.add<AudioOutput>("audioOut");
    audioOut.setInput("tape");
    audioOut.setVolume(0.7f);
    chain.audioOutput("audioOut");

    // =========================================================================
    // Visuals: Layered shapes + particles + feedback
    // =========================================================================

    // Background gradient
    auto& bg = chain.add<Gradient>("bg");
    bg.colorA.set(0.02f, 0.02f, 0.05f, 1.0f);
    bg.colorB.set(0.05f, 0.03f, 0.08f, 1.0f);
    bg.angle = 1.57f;  // Vertical

    // Central pulsing shape - responds to chord envelope
    auto& centerShape = chain.add<Shape>("centerShape");
    centerShape.type(ShapeType::Polygon);
    centerShape.sides = 6;
    centerShape.size.set(0.3f, 0.3f);
    centerShape.position.set(0.5f, 0.5f);
    centerShape.softness = 0.02f;
    centerShape.color.set(1.0f, 0.5f, 0.3f, 1.0f);

    // Orbiting ring shape
    auto& ringShape = chain.add<Shape>("ringShape");
    ringShape.type(ShapeType::Ring);
    ringShape.size.set(0.4f, 0.4f);
    ringShape.position.set(0.5f, 0.5f);
    ringShape.thickness = 0.02f;
    ringShape.softness = 0.01f;
    ringShape.color.set(0.3f, 0.7f, 1.0f, 0.6f);

    // Particles burst on chord changes
    auto& particles = chain.add<Particles>("particles");
    particles.emitter(EmitterShape::Disc);
    particles.position(0.5f, 0.5f);
    particles.emitterSize(0.15f);
    particles.emitRate(20.0f);
    particles.maxParticles(500);
    particles.radialVelocity(0.3f);
    particles.spread(360.0f);
    particles.velocityVariation(0.5f);
    particles.life(1.5f);
    particles.lifeVariation(0.3f);
    particles.size(0.015f, 0.005f);
    particles.gravity(0.0f);
    particles.drag(0.5f);
    particles.color(1.0f, 0.8f, 0.4f, 1.0f);
    particles.colorEnd(1.0f, 0.3f, 0.1f, 0.0f);
    particles.clearColor(0.0f, 0.0f, 0.0f, 0.0f);

    // Composite layers: bg + shapes + particles
    auto& comp1 = chain.add<Composite>("comp1");
    comp1.inputA(&bg);
    comp1.inputB(&centerShape);
    comp1.mode(BlendMode::Add);

    auto& comp2 = chain.add<Composite>("comp2");
    comp2.inputA(&comp1);
    comp2.inputB(&ringShape);
    comp2.mode(BlendMode::Add);

    auto& comp3 = chain.add<Composite>("comp3");
    comp3.inputA(&comp2);
    comp3.inputB(&particles);
    comp3.mode(BlendMode::Add);

    // Feedback for trailing effect
    auto& feedback = chain.add<Feedback>("feedback");
    feedback.input(&comp3);
    feedback.decay = 0.92f;
    feedback.mix = 0.3f;
    feedback.zoom = 1.01f;
    feedback.rotate = 0.005f;

    // Mirror for kaleidoscope
    auto& mirror = chain.add<Mirror>("mirror");
    mirror.input(&feedback);
    mirror.segments = 6;

    // Bloom for glow
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input(&mirror);
    bloom.threshold = 0.4f;
    bloom.intensity = 0.8f;
    bloom.radius = 15.0f;

    // Color grading
    auto& hsv = chain.add<HSV>("hsv");
    hsv.input(&bloom);
    hsv.saturation = 1.2f;
    hsv.value = 1.1f;

    chain.output("hsv");

    // Play first chord
    auto& s = chain.get<PolySynth>("synth");
    for (int i = 0; i < chords[0].count; ++i) {
        s.noteOn(chords[0].notes[i]);
    }
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float dt = ctx.dt();
    float time = ctx.time();

    auto& synth = chain.get<PolySynth>("synth");

    // =========================================================================
    // Chord progression
    // =========================================================================

    chordTime += dt;
    bool chordChanged = false;

    if (chordTime >= chordDuration) {
        chordTime = 0.0f;
        prevChordIndex = chordIndex;

        // Release current chord
        const ChordInfo& oldChord = chords[chordIndex];
        for (int i = 0; i < oldChord.count; ++i) {
            synth.noteOff(oldChord.notes[i]);
        }

        // Advance to next chord
        chordIndex = (chordIndex + 1) % numChords;
        chordChanged = true;

        // Play new chord
        const ChordInfo& newChord = chords[chordIndex];
        for (int i = 0; i < newChord.count; ++i) {
            synth.noteOn(newChord.notes[i]);
        }
    }

    // Smooth envelope - peaks at chord attack, decays over time
    float targetEnv = 1.0f - (chordTime / chordDuration);
    chordEnvelope = chordEnvelope * 0.95f + targetEnv * 0.05f;

    // =========================================================================
    // Visual responses to audio
    // =========================================================================

    const ChordInfo& chord = chords[chordIndex];
    float progress = chordTime / chordDuration;

    // Center shape: polygon sides match chord, size pulses with envelope
    auto& centerShape = chain.get<Shape>("centerShape");
    centerShape.sides = chord.polygonSides;
    float baseSize = 0.15f + chordEnvelope * 0.15f;
    float pulse = 0.02f * std::sin(time * 4.0f);
    centerShape.size.set(baseSize + pulse, baseSize + pulse);
    centerShape.rotation = time * 0.3f;

    // Center shape color from chord hue
    float h = chord.hue;
    float r = std::abs(std::fmod(h * 6.0f, 6.0f) - 3.0f) - 1.0f;
    float g = 2.0f - std::abs(std::fmod(h * 6.0f + 4.0f, 6.0f) - 3.0f);
    float b = 2.0f - std::abs(std::fmod(h * 6.0f + 2.0f, 6.0f) - 3.0f);
    r = std::clamp(r, 0.0f, 1.0f);
    g = std::clamp(g, 0.0f, 1.0f);
    b = std::clamp(b, 0.0f, 1.0f);
    centerShape.color.set(r, g, b, 0.9f);

    // Ring shape: rotates opposite, expands/contracts
    auto& ringShape = chain.get<Shape>("ringShape");
    float ringSize = 0.35f + 0.1f * std::sin(time * 0.7f) + chordEnvelope * 0.1f;
    ringShape.size.set(ringSize, ringSize);
    ringShape.rotation = -time * 0.2f;
    ringShape.thickness = 0.015f + 0.01f * chordEnvelope;

    // Particles: burst on chord change
    auto& particles = chain.get<Particles>("particles");
    if (chordChanged) {
        particles.burst(80);
    }
    particles.emitRate(15.0f + chordEnvelope * 40.0f);
    particles.color(r * 0.8f + 0.2f, g * 0.8f + 0.2f, b * 0.8f + 0.2f, 1.0f);

    // Mirror rotation follows chord progression
    auto& mirror = chain.get<Mirror>("mirror");
    mirror.angle = time * 0.1f + chordIndex * 0.5f;

    // Bloom intensity pulses with envelope
    auto& bloom = chain.get<Bloom>("bloom");
    bloom.intensity = 0.6f + chordEnvelope * 0.6f;

    // HSV hue shifts with chord
    auto& hsv = chain.get<HSV>("hsv");
    hsv.hueShift = chord.hue * 0.2f + progress * 0.05f;

    // Feedback parameters evolve slowly
    auto& feedback = chain.get<Feedback>("feedback");
    feedback.rotate = 0.003f + 0.004f * std::sin(time * 0.2f);

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
