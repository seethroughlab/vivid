#pragma once
#include "midi/midi_clip.h"   // vivid::session::ClipNote
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cmath>

// Pure, dependency-free note-list transforms shared by the clip editor (mouse/keyboard
// edits) and, later, the in-UI musical tools. Selection is an index mask parallel to the
// note list. No rendering/GLFW deps so these are unit-testable headless.
namespace vivid::session {

inline int sel_count(const std::vector<uint8_t>& sel) {
    int n = 0; for (uint8_t s : sel) n += s ? 1 : 0; return n;
}

// Shift selected notes by `dsemi` semitones, clamped to the MIDI range.
inline void transpose_selected(std::vector<ClipNote>& notes, const std::vector<uint8_t>& sel, int dsemi) {
    for (size_t i = 0; i < notes.size(); ++i)
        if (i < sel.size() && sel[i]) notes[i].pitch = std::clamp(notes[i].pitch + dsemi, 0, 127);
}

// Move selected notes by `dbeats`, clamped so they stay within [0, length].
inline void nudge_selected(std::vector<ClipNote>& notes, const std::vector<uint8_t>& sel,
                           double dbeats, double length) {
    for (size_t i = 0; i < notes.size(); ++i)
        if (i < sel.size() && sel[i])
            notes[i].start = std::clamp(notes[i].start + dbeats, 0.0, std::max(0.0, length - notes[i].dur));
}

// Snap selected note starts to the grid.
inline void quantize_selected(std::vector<ClipNote>& notes, const std::vector<uint8_t>& sel, double grid) {
    if (grid <= 0.0) return;
    for (size_t i = 0; i < notes.size(); ++i)
        if (i < sel.size() && sel[i]) notes[i].start = std::round(notes[i].start / grid) * grid;
}

// Set the velocity of selected notes (clamped 0..1).
inline void set_velocity_selected(std::vector<ClipNote>& notes, const std::vector<uint8_t>& sel, float v) {
    v = std::clamp(v, 0.f, 1.f);
    for (size_t i = 0; i < notes.size(); ++i) if (i < sel.size() && sel[i]) notes[i].vel = v;
}

// The selected notes, rebased so the earliest selected start is 0 (for the clipboard).
inline std::vector<ClipNote> copy_selected(const std::vector<ClipNote>& notes,
                                           const std::vector<uint8_t>& sel) {
    std::vector<ClipNote> out;
    double lo = 1e18;
    for (size_t i = 0; i < notes.size(); ++i) if (i < sel.size() && sel[i]) lo = std::min(lo, notes[i].start);
    if (lo > 1e17) return out;
    for (size_t i = 0; i < notes.size(); ++i)
        if (i < sel.size() && sel[i]) { ClipNote n = notes[i]; n.start -= lo; out.push_back(n); }
    return out;
}

// Beat span (max end − min start) of a note set; 0 if empty.
inline double notes_span(const std::vector<ClipNote>& notes) {
    if (notes.empty()) return 0.0;
    double lo = 1e18, hi = -1e18;
    for (const auto& n : notes) { lo = std::min(lo, n.start); hi = std::max(hi, n.start + n.dur); }
    return std::max(0.0, hi - lo);
}

// Append `clip` (clipboard notes, based at 0) into `notes` starting at `at` beats,
// clamped to `length`; returns the range [first,last) of appended indices so the caller
// can select them.
inline void paste_at(std::vector<ClipNote>& notes, std::vector<uint8_t>& sel,
                     const std::vector<ClipNote>& clip, double at, double length,
                     size_t& first, size_t& last) {
    first = notes.size();
    for (const auto& c : clip) {
        ClipNote n = c;
        n.start = std::clamp(c.start + at, 0.0, std::max(0.0, length - n.dur));
        notes.push_back(n);
        sel.push_back(1);
    }
    last = notes.size();
}

}  // namespace vivid::session
