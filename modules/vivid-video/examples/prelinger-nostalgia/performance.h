#pragma once
/**
 * @file performance.h
 * @brief Live performance state and helper functions
 *
 * State management, section control, mood triggering, and console output
 * for the Prelinger Nostalgia performance.
 */

#include "music.h"
#include <vivid/chain.h>
#include <vivid/audio/poly_synth.h>
#include <vivid/audio/wavetable_synth.h>
#include <vivid/audio/fm_synth.h>
#include <vivid/audio/song.h>
#include <iostream>
#include <string>
#include <vector>

namespace prelinger {

using namespace vivid;
using namespace vivid::audio;

// =========================================================================
// Song Structure - Section Definitions
// =========================================================================

struct SectionDef {
    const char* name;
    uint32_t startBar;
    uint32_t endBar;
    int mood;           // Which chord voicing to use
    bool triggerBells;  // Auto-trigger bells at section start
};

// Song structure: 72 bars at 72 BPM = 60 seconds
inline const SectionDef SECTIONS[] = {
    {"intro",   0,  8,  0, false},  // Am9 - Dreamy, sparse
    {"verse1",  8,  24, 1, false},  // Fmaj7 - Warm, building
    {"chorus",  24, 32, 2, true},   // Dm7 - Introspective, bells
    {"verse2",  32, 48, 3, false},  // Em7 - Mysterious
    {"chorus2", 48, 56, 2, true},   // Dm7 - Introspective, bells
    {"outro",   56, 72, 0, false},  // Am9 - Dreamy, fading
};

inline constexpr int NUM_SECTIONS = sizeof(SECTIONS) / sizeof(SECTIONS[0]);

// Find section index by name
inline int findSection(const std::string& name) {
    for (int i = 0; i < NUM_SECTIONS; ++i) {
        if (name == SECTIONS[i].name) return i;
    }
    return -1;
}

// =========================================================================
// Performance State
// =========================================================================

struct PerformanceState {
    int currentMood = 0;
    int currentSectionIdx = 0;
    bool isPaused = false;
    bool grainEnabled = true;
    bool crtEnabled = true;
    bool feedbackEnabled = true;
};

// Global state instance (inline for header-only)
inline PerformanceState g_state;

// =========================================================================
// Mood Triggering
// =========================================================================

inline void triggerMood(Chain& chain, int moodIdx) {
    if (moodIdx < 0 || moodIdx >= NUM_MOODS) return;

    auto& synth = chain.get<PolySynth>("synth");
    synth.allNotesOff();

    const auto& mood = MOODS[moodIdx];
    for (int i = 0; i < mood.count; ++i) {
        synth.noteOn(mood.notes[i]);
    }

    g_state.currentMood = moodIdx;
    std::cout << "Mood: " << mood.name << std::endl;
}

inline void triggerBells(Chain& chain) {
    auto& bells = chain.get<FMSynth>("bells");
    for (int i = 0; i < NUM_BELL_NOTES; ++i) {
        bells.noteOn(BELL_NOTES[i]);
    }
    std::cout << "Bells triggered\n";
}

inline void triggerLead(Chain& chain) {
    auto& lead = chain.get<WavetableSynth>("lead");
    lead.noteOn(LEAD_NOTE);
    std::cout << "Lead triggered\n";
}

// =========================================================================
// Section Control
// =========================================================================

inline void goToSection(Chain& chain, int sectionIdx) {
    if (sectionIdx < 0 || sectionIdx >= NUM_SECTIONS) return;

    auto& song = chain.get<Song>("song");
    const auto& section = SECTIONS[sectionIdx];

    // Jump to section
    song.jumpToSection(section.name);
    g_state.currentSectionIdx = sectionIdx;

    // Trigger the mood for this section
    triggerMood(chain, section.mood);

    // Trigger bells if section calls for it
    if (section.triggerBells) {
        triggerBells(chain);
    }

    std::cout << "Section: " << section.name << " (bars "
              << section.startBar << "-" << section.endBar << ")\n";
}

inline void nextSection(Chain& chain) {
    int next = (g_state.currentSectionIdx + 1) % NUM_SECTIONS;
    goToSection(chain, next);
}

inline void prevSection(Chain& chain) {
    int prev = g_state.currentSectionIdx - 1;
    if (prev < 0) prev = NUM_SECTIONS - 1;
    goToSection(chain, prev);
}

inline void restartSong(Chain& chain) {
    goToSection(chain, 0);
    std::cout << "Song restarted\n";
}

inline void skipToChorus(Chain& chain) {
    int chorusIdx = findSection("chorus");
    if (chorusIdx >= 0) {
        goToSection(chain, chorusIdx);
    }
}

// Called when song auto-advances to a new section
inline void onSectionChange(Chain& chain, const std::string& sectionName) {
    int idx = findSection(sectionName);
    if (idx >= 0 && idx != g_state.currentSectionIdx) {
        g_state.currentSectionIdx = idx;
        const auto& section = SECTIONS[idx];

        // Trigger mood for new section
        triggerMood(chain, section.mood);

        // Trigger bells if section calls for it
        if (section.triggerBells) {
            triggerBells(chain);
        }

        std::cout << "Section: " << sectionName << "\n";
    }
}

// =========================================================================
// Pause Control
// =========================================================================

inline void togglePause(Chain& chain) {
    g_state.isPaused = !g_state.isPaused;

    if (g_state.isPaused) {
        chain.get<PolySynth>("synth").allNotesOff();
        chain.get<WavetableSynth>("lead").allNotesOff();
        chain.get<FMSynth>("bells").allNotesOff();
        std::cout << "[PAUSED]\n";
    } else {
        triggerMood(chain, g_state.currentMood);
        std::cout << "[RESUMED]\n";
    }
}

// =========================================================================
// Console Output
// =========================================================================

inline void printStartupBanner() {
    std::cout << "\n";
    std::cout << "============================================\n";
    std::cout << "Prelinger Nostalgia - MIDImix Performance\n";
    std::cout << "============================================\n";
    std::cout << "\n";
    std::cout << "Song Structure (72 bars @ 72 BPM = 60 sec):\n";
    std::cout << "  intro(0-8) -> verse1(8-24) -> chorus(24-32)\n";
    std::cout << "  -> verse2(32-48) -> chorus2(48-56) -> outro(56-72)\n";
    std::cout << "\n";
    std::cout << "Controller: Akai MIDImix\n";
    std::cout << "\n";
    std::cout << "FADERS (mix levels):\n";
    std::cout << "  Ch1: Pad | Ch2: Lead | Ch3: Bells | Ch4: Clouds\n";
    std::cout << "  Ch5: Delay | Ch6: Reverb | Ch7: Tape | Ch8: Master\n";
    std::cout << "\n";
    std::cout << "KNOBS Row 1: Filters & Time FX\n";
    std::cout << "KNOBS Row 2: Tape & Granular\n";
    std::cout << "KNOBS Row 3: Visual Effects\n";
    std::cout << "\n";
    std::cout << "SOLO buttons: Prev | Next | Restart | Chorus | Bells\n";
    std::cout << "MUTE buttons: Grain | CRT | Feedback | Flash | Pause | Freeze\n";
    std::cout << "============================================\n\n";
}

} // namespace prelinger
