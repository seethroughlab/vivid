#pragma once
#include "midi/midi_clip.h"   // vivid::session::ClipNote / ExprCurve / AXIS_*
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <random>

// Pure musical transforms surfaced as editor actions (M5). Each operates on the target
// set = the selection, or the whole clip when nothing is selected. Dependency-free and
// header-only so they're unit-testable headless and reusable (editor + future MCP).
namespace vivid::session {

// The indices a tool acts on: the selection, or all notes when the selection is empty.
inline std::vector<size_t> tool_targets(const std::vector<uint8_t>& sel, size_t n) {
    std::vector<size_t> t;
    bool any = false;
    for (uint8_t s : sel) if (s) { any = true; break; }
    for (size_t i = 0; i < n; ++i) if (!any || (i < sel.size() && sel[i])) t.push_back(i);
    return t;
}

// Mirror target pitches around the center of their range (lo+hi)/2.
inline void invert_pitches(std::vector<ClipNote>& notes, const std::vector<uint8_t>& sel) {
    auto t = tool_targets(sel, notes.size());
    if (t.empty()) return;
    int lo = 127, hi = 0;
    for (size_t i : t) { lo = std::min(lo, notes[i].pitch); hi = std::max(hi, notes[i].pitch); }
    const int axis2 = lo + hi;   // 2 * center
    for (size_t i : t) notes[i].pitch = std::clamp(axis2 - notes[i].pitch, 0, 127);
}

// Reverse target notes in time within their bounding span [minStart, maxEnd].
inline void retrograde(std::vector<ClipNote>& notes, const std::vector<uint8_t>& sel) {
    auto t = tool_targets(sel, notes.size());
    if (t.size() < 2) return;
    double lo = 1e18, hi = -1e18;
    for (size_t i : t) { lo = std::min(lo, notes[i].start); hi = std::max(hi, notes[i].start + notes[i].dur); }
    for (size_t i : t) notes[i].start = lo + hi - (notes[i].start + notes[i].dur);
}

// Seeded jitter on start time (beats) and velocity — deterministic given `seed`.
inline void humanize(std::vector<ClipNote>& notes, const std::vector<uint8_t>& sel,
                     double time_amt, float vel_amt, uint32_t seed) {
    auto t = tool_targets(sel, notes.size());
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> d(-1.0, 1.0);
    for (size_t i : t) {
        notes[i].start = std::max(0.0, notes[i].start + d(rng) * time_amt);
        notes[i].vel = std::clamp(notes[i].vel + static_cast<float>(d(rng)) * vel_amt, 0.f, 1.f);
    }
}

// Snap target pitches to the nearest scale degree. `root` 0..11; `mask` bit c set =
// pitch-class c (relative to root) is in the scale. No-op if mask is empty.
inline void quantize_to_scale(std::vector<ClipNote>& notes, const std::vector<uint8_t>& sel,
                              int root, uint16_t mask) {
    if (mask == 0) return;
    root = ((root % 12) + 12) % 12;
    auto t = tool_targets(sel, notes.size());
    for (size_t i : t) {
        const int p = notes[i].pitch;
        int best = p;
        for (int d = 0; d <= 6; ++d) {
            const int up = ((((p + d) - root) % 12) + 12) % 12;
            const int dn = ((((p - d) - root) % 12) + 12) % 12;
            if (mask & (1u << up)) { best = p + d; break; }
            if (mask & (1u << dn)) { best = p - d; break; }
        }
        notes[i].pitch = std::clamp(best, 0, 127);
    }
}

// Stagger the starts of simultaneous (chord) target notes by `off` beats, low pitch
// first — the classic strum. Notes are grouped by (near-)equal start time.
inline void strum(std::vector<ClipNote>& notes, const std::vector<uint8_t>& sel, double off) {
    auto t = tool_targets(sel, notes.size());
    std::sort(t.begin(), t.end(), [&](size_t a, size_t b) {
        if (std::fabs(notes[a].start - notes[b].start) > 1e-6) return notes[a].start < notes[b].start;
        return notes[a].pitch < notes[b].pitch;
    });
    double group_start = -1e18; int k = 0;
    for (size_t idx : t) {
        if (std::fabs(notes[idx].start - group_start) > 1e-6) { group_start = notes[idx].start; k = 0; }
        notes[idx].start = group_start + k * off;
        ++k;
    }
}

// Fluid-Chords-style glide: each target note (in start order) after the first gets a
// bend curve starting at the previous note's pitch and ramping to 0 over the first
// `frac` of its length — so it slides in from the prior pitch. Clamped to ±maxsemi.
inline void apply_glide(std::vector<ClipNote>& notes, const std::vector<uint8_t>& sel,
                        float frac, float maxsemi) {
    auto t = tool_targets(sel, notes.size());
    std::sort(t.begin(), t.end(), [&](size_t a, size_t b) { return notes[a].start < notes[b].start; });
    frac = std::clamp(frac, 0.02f, 0.95f);
    for (size_t j = 1; j < t.size(); ++j) {
        const int from = notes[t[j - 1]].pitch, to = notes[t[j]].pitch;
        float delta = std::clamp(static_cast<float>(from - to), -maxsemi, maxsemi);
        ExprCurve& c = notes[t[j]].expr[AXIS_BEND];
        c.bp = { {0.f, delta}, {frac, 0.f}, {1.f, 0.f} };
    }
}

}  // namespace vivid::session
