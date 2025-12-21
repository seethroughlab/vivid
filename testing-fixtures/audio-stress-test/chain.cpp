// Audio Stress Test
//
// Tests audio system robustness under heavy load:
// - Multiple drum voices triggered at high rate (180 BPM)
// - Multiple synth voices with polyphony
// - Effects chain on each voice
// - Real-time parameter modulation
//
// Run for extended periods and listen for:
// - Clicks/pops (buffer underruns)
// - Timing drift (sequencer getting off beat)
// - Audio dropouts

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <vivid/audio_output.h>
#include <iostream>
#include <iomanip>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

// Stress test configuration
constexpr float BPM = 180.0f;           // Fast tempo for stress
constexpr int NUM_DRUM_VOICES = 4;       // Kick, snare, hihat, clap
constexpr int SYNTH_POLYPHONY = 8;       // Max simultaneous synth notes

// Timing stats
static uint64_t triggerCount = 0;
static float lastReportTime = 0.0f;
static float startTime = 0.0f;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // Clock - fast tempo for stress testing
    // =========================================================================

    auto& clock = chain.add<Clock>("clock");
    clock.bpm = BPM;
    clock.division(ClockDiv::Sixteenth);

    // =========================================================================
    // Drum machine - 4 voices with individual sequencers
    // =========================================================================

    // Kick - four on the floor
    auto& kickSeq = chain.add<Sequencer>("kickSeq");
    kickSeq.steps = 16;
    kickSeq.setPattern(0b0001000100010001);

    auto& kick = chain.add<Kick>("kick");
    kick.pitch = 45.0f;
    kick.pitchEnv = 120.0f;
    kick.decay = 0.15f;  // Short for clear transients
    kick.drive = 0.3f;

    // Snare - backbeat
    auto& snareSeq = chain.add<Sequencer>("snareSeq");
    snareSeq.steps = 16;
    snareSeq.setPattern(0b0000000100000001);

    auto& snare = chain.add<Snare>("snare");
    snare.tone = 0.6f;
    snare.noise = 0.7f;
    snare.snappy = 0.5f;
    snare.toneDecay = 0.05f;   // Short for clear transients
    snare.noiseDecay = 0.08f;

    // Hi-hat - busy pattern via euclidean
    auto& hatSeq = chain.add<Euclidean>("hatSeq");
    hatSeq.steps = 16;
    hatSeq.hits = 11;  // Dense pattern

    auto& hihat = chain.add<HiHat>("hihat");
    hihat.decay = 0.08f;
    hihat.tone = 0.6f;

    // Clap - syncopated
    auto& clapSeq = chain.add<Sequencer>("clapSeq");
    clapSeq.steps = 16;
    clapSeq.setPattern(0b0010001000100010);

    auto& clap = chain.add<Clap>("clap");
    clap.decay = 0.1f;   // Short for clear transients
    clap.spread = 0.6f;

    // =========================================================================
    // Polyphonic synth with arpeggio
    // =========================================================================

    auto& synth = chain.add<PolySynth>("synth");
    synth.waveform(Waveform::Saw);
    synth.attack = 0.01f;
    synth.decay = 0.08f;
    synth.sustain = 0.3f;
    synth.release = 0.1f;  // Short for clear transients
    synth.volume = 0.3f;

    // Arp sequencer
    auto& arpSeq = chain.add<Euclidean>("arpSeq");
    arpSeq.steps = 16;
    arpSeq.hits = 7;

    // =========================================================================
    // Effects - reverb and delay for additional CPU load
    // =========================================================================

    // Drum submix
    auto& drumMix = chain.add<AudioMixer>("drumMix");
    drumMix.setInput(0, "kick");
    drumMix.setGain(0, 0.5f);  // Tamed to not overwhelm
    drumMix.setInput(1, "snare");
    drumMix.setGain(1, 0.7f);
    drumMix.setInput(2, "hihat");
    drumMix.setGain(2, 0.5f);
    drumMix.setInput(3, "clap");
    drumMix.setGain(3, 0.6f);

    // Delay on drums (minimal for timing clarity)
    auto& drumDelay = chain.add<Delay>("drumDelay");
    drumDelay.input("drumMix");
    drumDelay.delayTime = 166.0f;  // Dotted eighth at 180 BPM (in ms)
    drumDelay.feedback = 0.15f;    // Low feedback for clarity
    drumDelay.mix = 0.15f;         // Subtle mix

    // Reverb on synth (minimal for timing clarity)
    auto& synthVerb = chain.add<Reverb>("synthVerb");
    synthVerb.input("synth");
    synthVerb.roomSize = 0.4f;     // Smaller room
    synthVerb.damping = 0.7f;      // More damping
    synthVerb.mix = 0.15f;         // Subtle mix

    // Master mix
    auto& master = chain.add<AudioMixer>("master");
    master.setInput(0, "drumDelay");
    master.setGain(0, 0.7f);
    master.setInput(1, "synthVerb");
    master.setGain(1, 0.5f);

    // Limiter on master
    auto& limiter = chain.add<Limiter>("limiter");
    limiter.input("master");
    limiter.ceiling = -0.5f;    // dB
    limiter.release = 100.0f;   // ms

    // Output
    auto& audioOut = chain.add<AudioOutput>("audioOut");
    audioOut.setInput("limiter");
    chain.audioOutput("audioOut");

    // =========================================================================
    // Visuals - simple indicator
    // =========================================================================

    auto& noise = chain.add<Noise>("noise");
    noise.scale = 4.0f;

    auto& flash = chain.add<Flash>("flash");
    flash.input("noise");
    flash.decay = 0.9f;
    flash.color.set(0.2f, 0.8f, 0.4f);

    chain.output("flash");

    // =========================================================================
    // Trigger callbacks - the stress test!
    // =========================================================================

    auto* chainPtr = &chain;

    kickSeq.onTrigger([chainPtr](float vel) {
        chainPtr->get<Kick>("kick").trigger();
        chainPtr->get<Flash>("flash").trigger(vel);
        triggerCount++;
    });

    snareSeq.onTrigger([chainPtr](float vel) {
        chainPtr->get<Snare>("snare").trigger();
        triggerCount++;
    });

    hatSeq.onTrigger([chainPtr]() {
        chainPtr->get<HiHat>("hihat").trigger();
        triggerCount++;
    });

    clapSeq.onTrigger([chainPtr](float vel) {
        chainPtr->get<Clap>("clap").trigger();
        triggerCount++;
    });

    // Arpeggio notes (minor scale)
    static const float arpNotes[] = {
        130.81f, 155.56f, 164.81f, 196.00f,  // C3, Eb3, E3, G3
        261.63f, 311.13f, 329.63f, 392.00f   // C4, Eb4, E4, G4
    };
    static int arpIndex = 0;

    arpSeq.onTrigger([chainPtr]() {
        auto& synth = chainPtr->get<PolySynth>("synth");
        synth.allNotesOff();
        synth.noteOn(arpNotes[arpIndex % 8]);
        arpIndex++;
        triggerCount++;
    });

    std::cout << "\n";
    std::cout << "===========================================\n";
    std::cout << "  AUDIO STRESS TEST\n";
    std::cout << "===========================================\n";
    std::cout << "BPM: " << BPM << " (16th notes)\n";
    std::cout << "Voices: " << NUM_DRUM_VOICES << " drums + poly synth\n";
    std::cout << "Effects: Delay, Reverb, Limiter\n";
    std::cout << "\n";
    std::cout << "Listen for:\n";
    std::cout << "  - Clicks or pops (buffer underruns)\n";
    std::cout << "  - Timing drift (tempo inconsistency)\n";
    std::cout << "  - Audio dropouts\n";
    std::cout << "\n";
    std::cout << "Press ESC to exit\n";
    std::cout << "===========================================\n\n";
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = ctx.time();

    if (startTime == 0.0f) {
        startTime = time;
    }

    // Advance clock
    auto& clock = chain.get<Clock>("clock");

    if (clock.triggered()) {
        chain.get<Sequencer>("kickSeq").advance();
        chain.get<Sequencer>("snareSeq").advance();
        chain.get<Euclidean>("hatSeq").advance();
        chain.get<Sequencer>("clapSeq").advance();
        chain.get<Euclidean>("arpSeq").advance();
    }

    // Modulate parameters (adds CPU load + tests param updates)
    float lfo = std::sin(time * 2.0f) * 0.5f + 0.5f;
    chain.get<Delay>("drumDelay").feedback = 0.1f + lfo * 0.1f;   // Keep subtle
    chain.get<Reverb>("synthVerb").roomSize = 0.3f + lfo * 0.2f;  // Keep small

    // Print stats every 5 seconds
    if (time - lastReportTime >= 5.0f) {
        float elapsed = time - startTime;
        float triggersPerSec = triggerCount / elapsed;
        float expectedPerSec = (BPM / 60.0f) * 4.0f * 5.0f;  // 5 sequencers, 16th notes

        std::cout << std::fixed << std::setprecision(1);
        std::cout << "[" << elapsed << "s] ";
        std::cout << "Triggers: " << triggerCount;
        std::cout << " (" << triggersPerSec << "/s";
        std::cout << ", expected ~" << expectedPerSec << "/s)\n";

        lastReportTime = time;
    }

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
