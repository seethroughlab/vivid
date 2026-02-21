// Ambient Melody - A musical composition in A minor
// Demonstrates melodic sequencing with pads, lead, and subtle drums
// Structure: Verse -> Chorus -> Verse -> Bridge -> Chorus -> Outro

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <vivid/audio_output.h>
#include <iostream>
#include <cmath>
#include <array>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

// =============================================================================
// Musical Constants (A minor)
// =============================================================================

namespace notes {
    constexpr float A3  = 220.00f;
    constexpr float B3  = 246.94f;
    constexpr float C4  = 261.63f;
    constexpr float D4  = 293.66f;
    constexpr float E4  = 329.63f;
    constexpr float F4  = 349.23f;
    constexpr float G4  = 392.00f;
    constexpr float A4  = 440.00f;
    constexpr float B4  = 493.88f;
    constexpr float C5  = 523.25f;
    constexpr float D5  = 587.33f;
    constexpr float E5  = 659.25f;
    constexpr float REST = 0.0f;
}

// =============================================================================
// Song Structure
// =============================================================================

enum class SongSection { Intro, Verse1, Chorus1, Verse2, Bridge, Chorus2, Outro, End };

static SongSection currentSection = SongSection::Intro;
static int sectionBar = 0;
static int totalBars = 0;
static int stepInBar = 0;
static int stepInPhrase = 0;
static float lastLeadNote = 0.0f;

constexpr int INTRO_BARS = 4;
constexpr int VERSE_BARS = 8;
constexpr int CHORUS_BARS = 8;
constexpr int BRIDGE_BARS = 4;
constexpr int OUTRO_BARS = 4;

// =============================================================================
// Melody Sequences (8 steps per bar for 8th notes at the lead tempo)
// =============================================================================

// Verse melody - contemplative, sparse (8th note resolution)
const std::array<float, 16> verseMelody = {
    notes::A4, notes::REST, notes::E4, notes::REST,
    notes::REST, notes::C5, notes::B4, notes::REST,
    notes::A4, notes::REST, notes::REST, notes::G4,
    notes::E4, notes::REST, notes::REST, notes::REST
};

// Chorus melody - more active
const std::array<float, 16> chorusMelody = {
    notes::E5, notes::D5, notes::C5, notes::REST,
    notes::B4, notes::A4, notes::REST, notes::G4,
    notes::A4, notes::REST, notes::B4, notes::C5,
    notes::E5, notes::D5, notes::C5, notes::REST
};

// Bridge melody - tension
const std::array<float, 16> bridgeMelody = {
    notes::F4, notes::REST, notes::G4, notes::REST,
    notes::A4, notes::REST, notes::B4, notes::REST,
    notes::C5, notes::REST, notes::D5, notes::REST,
    notes::E5, notes::REST, notes::REST, notes::REST
};

// Chord roots for each bar (pads will play these)
const std::array<float, 4> verseChords = { notes::A3, notes::F4 * 0.5f, notes::C4 * 0.5f, notes::G4 * 0.5f };
const std::array<float, 4> chorusChords = { notes::C4 * 0.5f, notes::G4 * 0.5f, notes::A3, notes::E4 * 0.5f };
const std::array<float, 4> bridgeChords = { notes::F4 * 0.5f, notes::G4 * 0.5f, notes::A3, notes::A3 };

// Visual state
static float kickVisual = 0.0f;
static float hihatVisual = 0.0f;
static float leadVisual = 0.0f;
static float padVisual = 0.0f;
static float lastPadRoot = 0.0f;

const char* getSectionName(SongSection s) {
    switch (s) {
        case SongSection::Intro: return "Intro";
        case SongSection::Verse1: return "Verse 1";
        case SongSection::Chorus1: return "Chorus";
        case SongSection::Verse2: return "Verse 2";
        case SongSection::Bridge: return "Bridge";
        case SongSection::Chorus2: return "Chorus";
        case SongSection::Outro: return "Outro";
        case SongSection::End: return "End";
        default: return "?";
    }
}

void printStatus() {
    std::cout << "\r[" << getSectionName(currentSection) << "] Bar "
              << (sectionBar + 1) << " | Total: " << totalBars << "   " << std::flush;
}

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Clock - 80 BPM, 8th notes for lead melody
    auto& clock = chain.add<Clock>("clock");
    clock.bpm = 80.0f;
    clock.division(ClockDiv::Eighth);
    clock.swing = 0.05f;

    // Drum sequencers - advance from clock on audio thread
    auto& kickSeq = chain.add<Sequencer>("kickSeq");
    kickSeq.setTriggerSource("clock");
    kickSeq.setStep(0,  {.velocity = 0.7f});                          // Gentle downbeat
    kickSeq.setStep(12, {.velocity = 0.55f, .microTiming = -0.05f});  // Softer, slightly late

    // Hihats with velocity variation and gate for open/closed feel
    auto& hihatSeq = chain.add<Sequencer>("hihatSeq");
    hihatSeq.setTriggerSource("clock");
    hihatSeq.setStep(0,  {.velocity = 0.6f, .gate = 0.3f});
    hihatSeq.setStep(2,  {.velocity = 0.4f, .gate = 0.2f, .microTiming = 0.04f});
    hihatSeq.setStep(4,  {.velocity = 0.65f, .gate = 0.4f});          // Slightly open
    hihatSeq.setStep(6,  {.velocity = 0.35f, .gate = 0.2f, .microTiming = 0.04f});
    hihatSeq.setStep(8,  {.velocity = 0.6f, .gate = 0.3f});
    hihatSeq.setStep(10, {.velocity = 0.4f, .gate = 0.2f, .microTiming = 0.04f});
    hihatSeq.setStep(12, {.velocity = 0.7f, .gate = 0.5f});           // Open accent
    hihatSeq.setStep(14, {.velocity = 0.35f, .gate = 0.2f, .microTiming = 0.04f});

    // Drums - trigger from sequencers on audio thread
    auto& kick = chain.add<Kick>("kick");
    kick.pitch = 42.0f;
    kick.pitchEnv = 50.0f;
    kick.pitchDecay = 0.12f;
    kick.decay = 0.5f;
    kick.click = 0.1f;
    kick.drive = 0.0f;
    kick.volume = 0.35f;

    auto& hihat = chain.add<HiHat>("hihat");
    hihat.decay = 0.025f;
    hihat.tone = 0.9f;
    hihat.ring = 0.15f;
    hihat.volume = 0.12f;

    // Drums trigger automatically from sequencers
    kick.setTriggerSource("kickSeq");
    hihat.setTriggerSource("hihatSeq");

    // Lead synth - saw wave with envelope
    auto& lead = chain.add<Synth>("lead");
    lead.setWaveform(Waveform::Saw);
    lead.attack = 0.03f;
    lead.decay = 0.2f;
    lead.sustain = 0.4f;
    lead.release = 0.25f;
    lead.volume = 0.25f;

    // Pad synths - long envelopes for smooth chords
    auto& pad1 = chain.add<Synth>("pad1");
    pad1.setWaveform(Waveform::Sine);
    pad1.attack = 0.8f;
    pad1.decay = 0.5f;
    pad1.sustain = 0.6f;
    pad1.release = 1.0f;
    pad1.volume = 0.15f;

    auto& pad2 = chain.add<Synth>("pad2");
    pad2.setWaveform(Waveform::Sine);
    pad2.attack = 0.9f;
    pad2.decay = 0.5f;
    pad2.sustain = 0.5f;
    pad2.release = 1.2f;
    pad2.detune = 3.0f;  // Slight detune for shimmer
    pad2.volume = 0.12f;

    auto& pad3 = chain.add<Synth>("pad3");
    pad3.setWaveform(Waveform::Triangle);
    pad3.attack = 1.0f;
    pad3.decay = 0.5f;
    pad3.sustain = 0.4f;
    pad3.release = 1.5f;
    pad3.volume = 0.08f;

    // Mixer
    auto& mixer = chain.add<AudioMixer>("mixer");
    mixer.input(0, "kick");
    mixer.gain(0, 1.0f);
    mixer.input(1, "hihat");
    mixer.gain(1, 1.0f);
    mixer.input(2, "lead");
    mixer.gain(2, 1.0f);
    mixer.input(3, "pad1");
    mixer.gain(3, 1.0f);
    mixer.input(4, "pad2");
    mixer.gain(4, 1.0f);
    mixer.input(5, "pad3");
    mixer.gain(5, 1.0f);
    mixer.volume = 0.85f;

    auto& audioOut = chain.add<AudioOutput>("audioOut");
    audioOut.input("mixer");
    audioOut.setVolume(1.0f);
    chain.audioOutput("audioOut");

    // Visuals (position 0-1, with 0.5,0.5 = center)
    auto& bg = chain.add<SolidColor>("bg");
    bg.color.set(0.02f, 0.02f, 0.04f, 1.0f);

    auto& padVis = chain.add<Shape>("padVis");
    padVis.type = ShapeType::Ellipse;
    padVis.position.set(0.5f, 0.5f);
    padVis.size.set(0.35f, 0.35f);
    padVis.color.set(0.15f, 0.25f, 0.45f, 0.25f);
    padVis.softness = 0.6f;

    auto& leadVis = chain.add<Shape>("leadVis");
    leadVis.type = ShapeType::Ellipse;
    leadVis.position.set(0.5f, 0.55f);
    leadVis.size.set(0.08f, 0.08f);
    leadVis.color.set(1.0f, 0.8f, 0.4f, 0.7f);
    leadVis.softness = 0.25f;

    auto& kickVis = chain.add<Shape>("kickVis");
    kickVis.type = ShapeType::Ellipse;
    kickVis.position.set(0.5f, 0.2f);
    kickVis.size.set(0.06f, 0.06f);
    kickVis.color.set(0.9f, 0.3f, 0.35f, 0.5f);
    kickVis.softness = 0.35f;

    auto& hihatVis = chain.add<Shape>("hihatVis");
    hihatVis.type = ShapeType::Ellipse;
    hihatVis.position.set(0.5f, 0.8f);
    hihatVis.size.set(0.03f, 0.03f);
    hihatVis.color.set(0.7f, 0.9f, 1.0f, 0.4f);
    hihatVis.softness = 0.4f;

    auto& comp = chain.add<Composite>("comp");
    comp.input(0, "bg");
    comp.input(1, "padVis");
    comp.input(2, "leadVis");
    comp.input(3, "kickVis");
    comp.input(4, "hihatVis");
    comp.mode = BlendMode::Add;

    chain.output("comp");

    std::cout << "\n========================================" << std::endl;
    std::cout << "Ambient Melody - A minor" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Controls: SPACE=Start/Stop, R=Restart, UP/DOWN=Tempo" << std::endl;
    std::cout << "========================================\n" << std::endl;
    printStatus();
}

void advanceSection() {
    sectionBar++;
    totalBars++;

    int sectionLength = 0;
    switch (currentSection) {
        case SongSection::Intro: sectionLength = INTRO_BARS; break;
        case SongSection::Verse1: sectionLength = VERSE_BARS; break;
        case SongSection::Chorus1: sectionLength = CHORUS_BARS; break;
        case SongSection::Verse2: sectionLength = VERSE_BARS; break;
        case SongSection::Bridge: sectionLength = BRIDGE_BARS; break;
        case SongSection::Chorus2: sectionLength = CHORUS_BARS; break;
        case SongSection::Outro: sectionLength = OUTRO_BARS; break;
        default: break;
    }

    if (sectionBar >= sectionLength) {
        sectionBar = 0;
        switch (currentSection) {
            case SongSection::Intro: currentSection = SongSection::Verse1; break;
            case SongSection::Verse1: currentSection = SongSection::Chorus1; break;
            case SongSection::Chorus1: currentSection = SongSection::Verse2; break;
            case SongSection::Verse2: currentSection = SongSection::Bridge; break;
            case SongSection::Bridge: currentSection = SongSection::Chorus2; break;
            case SongSection::Chorus2: currentSection = SongSection::Outro; break;
            case SongSection::Outro: currentSection = SongSection::End; break;
            default: break;
        }
        std::cout << std::endl;
    }
    printStatus();
}

float getMelodyNote(int step) {
    switch (currentSection) {
        case SongSection::Intro:
            return notes::REST;
        case SongSection::Verse1:
        case SongSection::Verse2:
            return verseMelody[step % verseMelody.size()];
        case SongSection::Chorus1:
        case SongSection::Chorus2:
            return chorusMelody[step % chorusMelody.size()];
        case SongSection::Bridge:
            return bridgeMelody[step % bridgeMelody.size()];
        case SongSection::Outro:
            return (sectionBar < 2) ? verseMelody[step % verseMelody.size()] : notes::REST;
        default:
            return notes::REST;
    }
}

float getPadRoot() {
    int idx = sectionBar % 4;
    switch (currentSection) {
        case SongSection::Intro:
        case SongSection::Verse1:
        case SongSection::Verse2:
        case SongSection::Outro:
            return verseChords[idx];
        case SongSection::Chorus1:
        case SongSection::Chorus2:
            return chorusChords[idx];
        case SongSection::Bridge:
            return bridgeChords[idx];
        default:
            return notes::A3;
    }
}

bool shouldPlayDrums() {
    if (currentSection == SongSection::Intro) return sectionBar >= 2;
    if (currentSection == SongSection::Outro) return sectionBar < 2;
    if (currentSection == SongSection::End) return false;
    return true;
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    if (currentSection == SongSection::End) return;

    auto& clock = chain.get<Clock>("clock");
    auto& kickSeq = chain.get<Sequencer>("kickSeq");
    auto& hihatSeq = chain.get<Sequencer>("hihatSeq");
    auto& kick = chain.get<Kick>("kick");
    auto& hihat = chain.get<HiHat>("hihat");
    auto& lead = chain.get<Synth>("lead");
    auto& pad1 = chain.get<Synth>("pad1");
    auto& pad2 = chain.get<Synth>("pad2");
    auto& pad3 = chain.get<Synth>("pad3");
    auto& padVis = chain.get<Shape>("padVis");
    auto& leadVis = chain.get<Shape>("leadVis");
    auto& kickVis = chain.get<Shape>("kickVis");
    auto& hihatVis = chain.get<Shape>("hihatVis");

    // Controls
    if (ctx.key(GLFW_KEY_SPACE).pressed) {
        if (clock.isRunning()) {
            clock.stop();
            lead.noteOff();
            pad1.noteOff(); pad2.noteOff(); pad3.noteOff();
        } else {
            clock.start();
        }
    }
    if (ctx.key(GLFW_KEY_R).pressed) {
        currentSection = SongSection::Intro;
        sectionBar = 0; totalBars = 0; stepInBar = 0; stepInPhrase = 0;
        lastLeadNote = 0.0f; lastPadRoot = 0.0f;
        clock.reset(); clock.start();
        kickSeq.reset(); hihatSeq.reset();
        lead.noteOff();
        pad1.noteOff(); pad2.noteOff(); pad3.noteOff();
        std::cout << "\n[Restarting...]" << std::endl;
        printStatus();
    }
    if (ctx.key(GLFW_KEY_UP).pressed) {
        clock.bpm = std::min(static_cast<float>(clock.bpm) + 5.0f, 120.0f);
        std::cout << "\n[BPM: " << static_cast<float>(clock.bpm) << "]" << std::endl;
        printStatus();
    }
    if (ctx.key(GLFW_KEY_DOWN).pressed) {
        clock.bpm = std::max(static_cast<float>(clock.bpm) - 5.0f, 50.0f);
        std::cout << "\n[BPM: " << static_cast<float>(clock.bpm) << "]" << std::endl;
        printStatus();
    }

    // NOTE: Sequencers advance and drums trigger automatically via setTriggerSource()
    // We only need to handle melodic parts and visual feedback here
    if (clock.triggered()) {
        // Lead melody (manual control for musical expression)
        float noteFreq = getMelodyNote(stepInPhrase);
        if (noteFreq > 0.0f) {
            // Only trigger on note CHANGE (not same note continuing)
            if (noteFreq != lastLeadNote) {
                if (lastLeadNote > 0.0f) {
                    lead.noteOff();
                }
                lead.frequency = noteFreq;
                lead.noteOn();
                lastLeadNote = noteFreq;
                leadVisual = 1.0f;
            }
            // Same note continues - do nothing (let it sustain)
        } else if (lastLeadNote > 0.0f) {
            // Rest - release note
            lead.noteOff();
            lastLeadNote = 0.0f;
        }

        // Visual feedback for drums (drums trigger automatically, but we track for visuals)
        if (shouldPlayDrums()) {
            if (kickSeq.triggered()) { kickVisual = 1.0f; }
            if (hihatSeq.triggered()) { hihatVisual = 1.0f; }
        }

        // Pads - only trigger when chord changes
        if (stepInBar == 0) {
            float root = getPadRoot();
            if (root != lastPadRoot) {
                // Chord changed - release old, start new
                if (lastPadRoot > 0.0f) {
                    pad1.noteOff(); pad2.noteOff(); pad3.noteOff();
                }

                pad1.frequency = root;
                pad2.frequency = root * 1.003f;  // Slight detune
                pad3.frequency = root * 1.5f;    // Fifth

                pad1.noteOn();
                pad2.noteOn();
                pad3.noteOn();
                lastPadRoot = root;
                padVisual = 0.7f;
            }
        }

        stepInBar = (stepInBar + 1) % 8;  // 8 8th notes per bar
        stepInPhrase = (stepInPhrase + 1) % 16;

        if (stepInBar == 0) {
            advanceSection();
        }
    }

    // Visuals
    float dt = ctx.dt();
    kickVisual *= (1.0f - dt * 6.0f);
    hihatVisual *= (1.0f - dt * 8.0f);
    leadVisual *= (1.0f - dt * 5.0f);
    padVisual *= (1.0f - dt * 1.5f);

    float padSize = 0.3f + padVisual * 0.1f;
    padVis.size.set(padSize, padSize);
    padVis.color.set(0.15f + padVisual * 0.1f, 0.25f + padVisual * 0.15f,
                     0.45f + padVisual * 0.2f, 0.2f + padVisual * 0.2f);

    float leadSize = 0.04f + leadVisual * 0.06f;
    leadVis.size.set(leadSize, leadSize);
    leadVis.color.set(1.0f, 0.8f + leadVisual * 0.1f, 0.4f + leadVisual * 0.3f,
                      0.3f + leadVisual * 0.6f);

    float kickSize = 0.04f + kickVisual * 0.06f;
    kickVis.size.set(kickSize, kickSize);
    kickVis.color.set(0.9f, 0.3f + kickVisual * 0.3f, 0.35f, 0.2f + kickVisual * 0.5f);

    float hihatSize = 0.02f + hihatVisual * 0.03f;
    hihatVis.size.set(hihatSize, hihatSize);
    hihatVis.color.set(0.7f, 0.9f, 1.0f, 0.15f + hihatVisual * 0.4f);
}

VIVID_CHAIN(setup, update)
