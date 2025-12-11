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

enum class Section { Intro, Verse1, Chorus1, Verse2, Bridge, Chorus2, Outro, End };

static Section currentSection = Section::Intro;
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

const char* getSectionName(Section s) {
    switch (s) {
        case Section::Intro: return "Intro";
        case Section::Verse1: return "Verse 1";
        case Section::Chorus1: return "Chorus";
        case Section::Verse2: return "Verse 2";
        case Section::Bridge: return "Bridge";
        case Section::Chorus2: return "Chorus";
        case Section::Outro: return "Outro";
        case Section::End: return "End";
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
    clock.bpm(80.0f).division(ClockDiv::Eighth).swing(0.05f);

    // Drum sequencers (16th note patterns, so 2 clock ticks per step)
    auto& kickSeq = chain.add<Sequencer>("kickSeq");
    kickSeq.setPattern(0x1001);  // Downbeats

    auto& hihatSeq = chain.add<Sequencer>("hihatSeq");
    hihatSeq.setPattern(0x5555);  // Every other 8th

    // Drums - very subtle
    auto& kick = chain.add<Kick>("kick");
    kick.pitch(42.0f).pitchEnv(50.0f).pitchDecay(0.12f)
        .decay(0.5f).click(0.1f).drive(0.0f).volume(0.35f);

    auto& hihat = chain.add<HiHat>("hihat");
    hihat.decay(0.025f).tone(0.9f).ring(0.15f).volume(0.12f);

    // Lead synth - saw wave with envelope
    auto& lead = chain.add<Synth>("lead");
    lead.waveform(Waveform::Saw)
        .attack(0.03f).decay(0.2f).sustain(0.4f).release(0.25f)
        .volume(0.25f);

    // Pad synths - long envelopes for smooth chords
    auto& pad1 = chain.add<Synth>("pad1");
    pad1.waveform(Waveform::Sine)
        .attack(0.8f).decay(0.5f).sustain(0.6f).release(1.0f)
        .volume(0.15f);

    auto& pad2 = chain.add<Synth>("pad2");
    pad2.waveform(Waveform::Sine)
        .attack(0.9f).decay(0.5f).sustain(0.5f).release(1.2f)
        .detune(3.0f)  // Slight detune for shimmer
        .volume(0.12f);

    auto& pad3 = chain.add<Synth>("pad3");
    pad3.waveform(Waveform::Triangle)
        .attack(1.0f).decay(0.5f).sustain(0.4f).release(1.5f)
        .volume(0.08f);

    // Mixer
    auto& mixer = chain.add<AudioMixer>("mixer");
    mixer.input(0, "kick").gain(0, 1.0f)
         .input(1, "hihat").gain(1, 1.0f)
         .input(2, "lead").gain(2, 1.0f)
         .input(3, "pad1").gain(3, 1.0f)
         .input(4, "pad2").gain(4, 1.0f)
         .input(5, "pad3").gain(5, 1.0f)
         .volume(0.85f);

    auto& audioOut = chain.add<AudioOutput>("audioOut");
    audioOut.input("mixer").volume(1.0f);
    chain.audioOutput("audioOut");

    // Visuals (position 0-1, with 0.5,0.5 = center)
    auto& bg = chain.add<SolidColor>("bg");
    bg.color(0.02f, 0.02f, 0.04f);

    auto& padVis = chain.add<Shape>("padVis");
    padVis.type(ShapeType::Circle).position(0.5f, 0.5f).size(0.35f)
          .color(0.15f, 0.25f, 0.45f, 0.25f).softness(0.6f);

    auto& leadVis = chain.add<Shape>("leadVis");
    leadVis.type(ShapeType::Circle).position(0.5f, 0.55f).size(0.08f)
           .color(1.0f, 0.8f, 0.4f, 0.7f).softness(0.25f);

    auto& kickVis = chain.add<Shape>("kickVis");
    kickVis.type(ShapeType::Circle).position(0.5f, 0.2f).size(0.06f)
           .color(0.9f, 0.3f, 0.35f, 0.5f).softness(0.35f);

    auto& hihatVis = chain.add<Shape>("hihatVis");
    hihatVis.type(ShapeType::Circle).position(0.5f, 0.8f).size(0.03f)
            .color(0.7f, 0.9f, 1.0f, 0.4f).softness(0.4f);

    auto& comp = chain.add<Composite>("comp");
    comp.input(0, &bg)
        .input(1, &padVis)
        .input(2, &leadVis)
        .input(3, &kickVis)
        .input(4, &hihatVis)
        .mode(BlendMode::Add);

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
        case Section::Intro: sectionLength = INTRO_BARS; break;
        case Section::Verse1: sectionLength = VERSE_BARS; break;
        case Section::Chorus1: sectionLength = CHORUS_BARS; break;
        case Section::Verse2: sectionLength = VERSE_BARS; break;
        case Section::Bridge: sectionLength = BRIDGE_BARS; break;
        case Section::Chorus2: sectionLength = CHORUS_BARS; break;
        case Section::Outro: sectionLength = OUTRO_BARS; break;
        default: break;
    }

    if (sectionBar >= sectionLength) {
        sectionBar = 0;
        switch (currentSection) {
            case Section::Intro: currentSection = Section::Verse1; break;
            case Section::Verse1: currentSection = Section::Chorus1; break;
            case Section::Chorus1: currentSection = Section::Verse2; break;
            case Section::Verse2: currentSection = Section::Bridge; break;
            case Section::Bridge: currentSection = Section::Chorus2; break;
            case Section::Chorus2: currentSection = Section::Outro; break;
            case Section::Outro: currentSection = Section::End; break;
            default: break;
        }
        std::cout << std::endl;
    }
    printStatus();
}

float getMelodyNote(int step) {
    switch (currentSection) {
        case Section::Intro:
            return notes::REST;
        case Section::Verse1:
        case Section::Verse2:
            return verseMelody[step % verseMelody.size()];
        case Section::Chorus1:
        case Section::Chorus2:
            return chorusMelody[step % chorusMelody.size()];
        case Section::Bridge:
            return bridgeMelody[step % bridgeMelody.size()];
        case Section::Outro:
            return (sectionBar < 2) ? verseMelody[step % verseMelody.size()] : notes::REST;
        default:
            return notes::REST;
    }
}

float getPadRoot() {
    int idx = sectionBar % 4;
    switch (currentSection) {
        case Section::Intro:
        case Section::Verse1:
        case Section::Verse2:
        case Section::Outro:
            return verseChords[idx];
        case Section::Chorus1:
        case Section::Chorus2:
            return chorusChords[idx];
        case Section::Bridge:
            return bridgeChords[idx];
        default:
            return notes::A3;
    }
}

bool shouldPlayDrums() {
    if (currentSection == Section::Intro) return sectionBar >= 2;
    if (currentSection == Section::Outro) return sectionBar < 2;
    if (currentSection == Section::End) return false;
    return true;
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    if (currentSection == Section::End) return;

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
        currentSection = Section::Intro;
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
        clock.bpm(std::min(clock.getBpm() + 5.0f, 120.0f));
        std::cout << "\n[BPM: " << clock.getBpm() << "]" << std::endl;
        printStatus();
    }
    if (ctx.key(GLFW_KEY_DOWN).pressed) {
        clock.bpm(std::max(clock.getBpm() - 5.0f, 50.0f));
        std::cout << "\n[BPM: " << clock.getBpm() << "]" << std::endl;
        printStatus();
    }

    // Sequencer
    if (clock.triggered()) {
        kickSeq.advance();
        hihatSeq.advance();

        // Lead melody
        float noteFreq = getMelodyNote(stepInPhrase);
        if (noteFreq > 0.0f) {
            // Only trigger on note CHANGE (not same note continuing)
            if (noteFreq != lastLeadNote) {
                if (lastLeadNote > 0.0f) {
                    lead.noteOff();
                }
                lead.frequency(noteFreq);
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

        // Drums
        if (shouldPlayDrums()) {
            if (kickSeq.triggered()) { kick.trigger(); kickVisual = 1.0f; }
            if (hihatSeq.triggered()) { hihat.trigger(); hihatVisual = 1.0f; }
        }

        // Pads - only trigger when chord changes
        if (stepInBar == 0) {
            float root = getPadRoot();
            if (root != lastPadRoot) {
                // Chord changed - release old, start new
                if (lastPadRoot > 0.0f) {
                    pad1.noteOff(); pad2.noteOff(); pad3.noteOff();
                }

                pad1.frequency(root);
                pad2.frequency(root * 1.003f);  // Slight detune
                pad3.frequency(root * 1.5f);    // Fifth

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

    padVis.size(0.3f + padVisual * 0.1f);
    padVis.color(0.15f + padVisual * 0.1f, 0.25f + padVisual * 0.15f,
                 0.45f + padVisual * 0.2f, 0.2f + padVisual * 0.2f);

    leadVis.size(0.04f + leadVisual * 0.06f);
    leadVis.color(1.0f, 0.8f + leadVisual * 0.1f, 0.4f + leadVisual * 0.3f,
                  0.3f + leadVisual * 0.6f);

    kickVis.size(0.04f + kickVisual * 0.06f);
    kickVis.color(0.9f, 0.3f + kickVisual * 0.3f, 0.35f, 0.2f + kickVisual * 0.5f);

    hihatVis.size(0.02f + hihatVisual * 0.03f);
    hihatVis.color(0.7f, 0.9f, 1.0f, 0.15f + hihatVisual * 0.4f);
}

VIVID_CHAIN(setup, update)
