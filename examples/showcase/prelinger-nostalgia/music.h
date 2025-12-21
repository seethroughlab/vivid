#pragma once
/**
 * @file music.h
 * @brief Musical content for Prelinger Nostalgia performance
 *
 * Chord voicings and note data inspired by Boards of Canada's
 * melancholic, nostalgic sound palette.
 */

#include <vivid/audio/notes.h>
#include <array>
#include <cstdint>

namespace prelinger {

using namespace vivid::audio::freq;

// =========================================================================
// Chord Voicings - BoC-inspired melancholic progressions
// =========================================================================

struct ChordVoicing {
    std::array<float, 6> notes;
    int count;
    const char* name;
};

// Four mood voicings, each with 6 notes for rich pads
inline const ChordVoicing MOODS[] = {
    // Mood 0: Am9 - dreamy, floating
    {{A2, E3, G3, B3, C4, E4}, 6, "Am9 (Dreamy)"},
    // Mood 1: Fmaj7 - warm, hopeful
    {{F2, C3, E3, A3, C4, E4}, 6, "Fmaj7 (Warm)"},
    // Mood 2: Dm7 - introspective
    {{D2, A2, C3, F3, A3, D4}, 6, "Dm7 (Introspective)"},
    // Mood 3: Em7 - mysterious
    {{E2, B2, D3, G3, B3, E4}, 6, "Em7 (Mysterious)"},
};

inline constexpr int NUM_MOODS = 4;

// =========================================================================
// Bell Chord - for ethereal accents
// =========================================================================

inline const float BELL_NOTES[] = {C5, G5};
inline constexpr int NUM_BELL_NOTES = 2;

// =========================================================================
// Lead Notes - for triggered melodic moments
// =========================================================================

inline const float LEAD_NOTE = E4;

} // namespace prelinger
