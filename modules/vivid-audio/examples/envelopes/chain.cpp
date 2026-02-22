/**
 * Envelopes Example
 *
 * Demonstrates: Envelope, AR, PitchEnv, ADSRMod
 *
 * Four envelope types applied to different musical contexts:
 * 1. Envelope (ADSR) - sustained pad with full ADSR shape
 * 2. AR - plucky one-shot sound with attack-release only
 * 3. PitchEnv - kick drum frequency sweep
 * 4. ADSRMod - per-voice filter cutoff modulation on a synth
 */

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <vivid/audio/modulators/adsr.h>

using namespace vivid;
using namespace vivid::audio;
using namespace vivid::effects;

// State for pad voice management
static int padNote = -1;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Master clock at 120 BPM
    auto& clock = chain.add<Clock>("clock");
    clock.bpm = 120.0f;
    clock.division(ClockDiv::Sixteenth);

    // =========================================================================
    // 1. ENVELOPE (ADSR) - Sustained pad sound
    // =========================================================================
    // Full attack-decay-sustain-release envelope for held notes.
    // The sustain phase holds at a level until releaseNote() is called.

    auto& padOsc = chain.add<Oscillator>("padOsc");
    padOsc.waveform(Waveform::Triangle);
    padOsc.frequency = 220.0f;
    padOsc.volume = 0.3f;

    auto& padEnv = chain.add<Envelope>("padEnv");
    padEnv.setInputByName(0, "padOsc");  // Multiplies oscillator by envelope
    padEnv.attack = 0.3f;           // Slow attack (pad-like)
    padEnv.decay = 0.2f;
    padEnv.sustain = 0.6f;          // Hold at 60%
    padEnv.release = 0.8f;          // Long release tail

    // =========================================================================
    // 2. AR - Plucky one-shot sound
    // =========================================================================
    // Two-stage envelope (attack → release), no sustain.
    // Ignores noteOff — fires and forgets. Perfect for plucks and bells.

    auto& pluckOsc = chain.add<Oscillator>("pluckOsc");
    pluckOsc.waveform(Waveform::Saw);
    pluckOsc.frequency = 440.0f;
    pluckOsc.volume = 0.25f;

    auto& pluckEnv = chain.add<AR>("pluckEnv");
    pluckEnv.setInputByName(0, "pluckOsc");
    pluckEnv.attack = 0.005f;       // Very fast attack (pluck)
    pluckEnv.release = 0.4f;        // Medium release

    // Sequencer drives the pluck on eighth notes
    auto& pluckSeq = chain.add<Sequencer>("pluckSeq");
    pluckSeq.setTriggerSource("clock");
    pluckSeq.setStep(0,  {.velocity = 0.9f});
    pluckSeq.setStep(4,  {.velocity = 0.6f});
    pluckSeq.setStep(6,  {.velocity = 0.7f});
    pluckSeq.setStep(10, {.velocity = 0.5f});
    pluckSeq.setStep(12, {.velocity = 0.8f});
    pluckEnv.setTriggerSource("pluckSeq");

    // =========================================================================
    // 3. PITCHENV - Kick drum frequency sweep
    // =========================================================================
    // Sweeps frequency from startFreq to endFreq over time.
    // Read currentFreq() in update() and apply to an oscillator.

    auto& kickOsc = chain.add<Oscillator>("kickOsc");
    kickOsc.waveform(Waveform::Sine);
    kickOsc.frequency = 50.0f;
    kickOsc.volume = 0.5f;

    auto& kickPitch = chain.add<PitchEnv>("kickPitch");
    kickPitch.startFreq = 180.0f;   // Start high
    kickPitch.endFreq = 45.0f;      // Sweep down to sub bass
    kickPitch.time = 0.08f;         // Fast sweep (80ms)

    auto& kickAmp = chain.add<AR>("kickAmp");
    kickAmp.setInputByName(0, "kickOsc");
    kickAmp.attack = 0.001f;        // Near-instant attack
    kickAmp.release = 0.25f;        // Quick release

    // Kick on beats 1 and 3
    auto& kickSeq = chain.add<Sequencer>("kickSeq");
    kickSeq.setTriggerSource("clock");
    kickSeq.setStep(0,  {.velocity = 1.0f});
    kickSeq.setStep(8,  {.velocity = 0.9f});
    kickPitch.setTriggerSource("kickSeq");
    kickAmp.setTriggerSource("kickSeq");

    // =========================================================================
    // 4. ADSRMOD - Per-voice filter modulation on synth
    // =========================================================================
    // ADSRMod attached to a WavetableSynth modulates filter cutoff per-voice.
    // Each voice gets its own independent envelope state.

    auto& synth = chain.add<WavetableSynth>("synth");
    synth.loadBuiltin(BuiltinTable::Analog);
    synth.maxVoices = 4;
    synth.volume = 0.3f;
    synth.filterCutoff = 500.0f;
    synth.filterResonance = 0.4f;

    // Attach ADSR modulator for filter envelope (per-voice)
    auto& filterEnv = synth.addModulator<ADSRMod>("filterEnv");
    filterEnv.attack = 0.01f;       // Fast plucky attack
    filterEnv.decay = 0.35f;        // Medium decay
    filterEnv.sustain = 0.15f;      // Low sustain (filter mostly closed)
    filterEnv.release = 0.4f;
    filterEnv.perVoice = true;      // Each voice has own envelope
    synth.modulate(filterEnv, "filterCutoff", 0.8f, false);  // Unipolar: 0→1

    // Synth sequence on quarter notes
    auto& synthSeq = chain.add<Sequencer>("synthSeq");
    synthSeq.setTriggerSource("clock");
    synthSeq.setTarget("synth");
    synthSeq.setStep(0,  {.note = 48, .velocity = 0.8f, .gate = 0.4f});
    synthSeq.setStep(4,  {.note = 55, .velocity = 0.7f, .gate = 0.3f});
    synthSeq.setStep(8,  {.note = 51, .velocity = 0.75f, .gate = 0.4f});
    synthSeq.setStep(12, {.note = 53, .velocity = 0.6f, .gate = 0.3f});

    // =========================================================================
    // Mix all four voices
    // =========================================================================

    auto& mixer = chain.add<AudioMixer>("mixer");
    mixer.input(0, "padEnv");      // ADSR pad
    mixer.input(1, "pluckEnv");    // AR pluck
    mixer.input(2, "kickAmp");     // Kick (PitchEnv + AR)
    mixer.input(3, "synth");       // Synth (ADSRMod filter)
    mixer.gain(0, 0.8f);
    mixer.gain(1, 0.7f);
    mixer.gain(2, 1.0f);
    mixer.gain(3, 0.8f);

    // Light reverb
    auto& reverb = chain.add<Reverb>("reverb");
    reverb.input("mixer");
    reverb.roomSize = 0.4f;
    reverb.damping = 0.5f;
    reverb.mix = 0.15f;

    // Audio output
    auto& out = chain.add<AudioOutput>("out");
    out.setInput("reverb");
    chain.audioOutput("out");

    // Visual output
    auto& visual = chain.add<Noise>("visual");
    visual.scale = 4.0f;
    chain.output("visual");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    // ---- PitchEnv: update kick oscillator frequency each frame ----
    auto& kickPitch = chain.get<PitchEnv>("kickPitch");
    auto& kickOsc = chain.get<Oscillator>("kickOsc");
    kickOsc.frequency = kickPitch.currentFreq();

    // ---- Envelope: trigger/release pad on a slow cycle (every 2s) ----
    auto& padEnv = chain.get<Envelope>("padEnv");
    auto& padOsc = chain.get<Oscillator>("padOsc");

    static float lastPadTime = -10.0f;
    static bool padHeld = false;

    if (!padHeld && t - lastPadTime >= 2.0f) {
        // Trigger pad note
        padEnv.trigger();
        padHeld = true;
        lastPadTime = t;
    } else if (padHeld && t - lastPadTime >= 1.5f) {
        // Release after 1.5s hold (sustain phase ends)
        padEnv.releaseNote();
        padHeld = false;
    }

    // Slowly change pad pitch
    float padFreq = 220.0f + 110.0f * std::sin(t * 0.15f);
    padOsc.frequency = padFreq;

    // ---- Pluck: vary pitch per trigger ----
    auto& pluckSeq = chain.get<Sequencer>("pluckSeq");
    auto& pluckOsc = chain.get<Oscillator>("pluckOsc");

    static const float pluckNotes[] = {440.0f, 523.25f, 587.33f, 659.26f, 523.25f};
    static int pluckIdx = 0;
    if (pluckSeq.triggered()) {
        pluckOsc.frequency = pluckNotes[pluckIdx];
        pluckIdx = (pluckIdx + 1) % 5;
    }

    // Visual pulse on kick
    auto& kickSeq = chain.get<Sequencer>("kickSeq");
    auto& visual = chain.get<Noise>("visual");

    if (kickSeq.triggered()) {
        visual.scale = 2.0f;
    } else {
        visual.scale = static_cast<float>(visual.scale) +
                       (4.0f - static_cast<float>(visual.scale)) * 0.05f;
    }

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
