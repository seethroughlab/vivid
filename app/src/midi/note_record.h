#pragma once
#include "midi/midi_clip.h"   // CcBp

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

// Pure transforms the recording commit needs. Kept here — module `midi`, rank 0 — rather than inside
// vst3_host.cpp so they can be tested with no app fixture and no VST3 SDK. They take raw arrays
// deliberately: `RecNote` lives in vst3_host_internal.h and dragging that in would pull the whole
// host with it.
namespace vivid::session {

// Extend each note that was released while the sustain pedal was DOWN to the pedal's next release.
// This is what makes a recorded piano part sound like what was played rather than like staccato
// stabs — the pedal is why a pianist's fingers leave the keys long before the notes stop.
//
// `pedal` is the captured CC64 stream in ABSOLUTE beats, value >= 0.5 meaning down. Pure, and
// O(n log n) in the pedal points. `beat_off` is modified in place.
inline void apply_sustain(const double* beat_on, double* beat_off, size_t n,
                          const std::vector<CcBp>& pedal) {
    if (!beat_on || !beat_off || n == 0 || pedal.empty()) return;

    // Collapse the raw stream into down/up transitions, so repeated "still down" points don't
    // multiply the work and a pedal that starts down is handled.
    std::vector<CcBp> ped = pedal;
    std::sort(ped.begin(), ped.end(), [](const CcBp& a, const CcBp& b) { return a.t < b.t; });
    std::vector<double> releases;      // beats at which the pedal went UP
    bool down = false;
    std::vector<std::pair<double, double>> held;   // [down_at, up_at) spans; up_at = +inf if never released
    double down_at = 0.0;
    for (const CcBp& p : ped) {
        const bool now_down = p.v >= 0.5f;
        if (now_down && !down) { down = true; down_at = p.t; }
        else if (!now_down && down) { down = false; held.emplace_back(down_at, p.t); }
    }
    if (down) held.emplace_back(down_at, std::numeric_limits<double>::infinity());
    if (held.empty()) return;

    for (size_t i = 0; i < n; ++i) {
        const double off = beat_off[i];
        // Which held span was the pedal in when this note was released? A note released with the
        // pedal UP is untouched; a note released before the pedal ever went down is untouched.
        for (const auto& span : held) {
            if (off >= span.first && off < span.second) {
                // Extend to the pedal release. (An unreleased pedal leaves the note as it was — the
                // caller closes still-open notes at the end of the take, and inventing an infinite
                // duration here would produce a note longer than the clip.)
                if (std::isfinite(span.second) && span.second > off) beat_off[i] = span.second;
                break;
            }
        }
    }
}

namespace detail {
// Ramer-Douglas-Peucker in value space, same shape as note_ops.h's rdp_keep but over CcBp
// (double-beat t rather than a normalized float).
inline void rdp_keep_cc(const std::vector<CcBp>& in, size_t i0, size_t i1, float eps,
                        std::vector<char>& keep) {
    if (i1 <= i0 + 1) return;
    const CcBp& a = in[i0];
    const CcBp& b = in[i1];
    const double span = b.t - a.t;
    float maxd = 0.f; size_t idx = i0;
    for (size_t i = i0 + 1; i < i1; ++i) {
        const float vt = span > 1e-12 ? static_cast<float>(a.v + (b.v - a.v) * ((in[i].t - a.t) / span)) : a.v;
        const float d = std::fabs(in[i].v - vt);
        if (d > maxd) { maxd = d; idx = i; }
    }
    if (maxd > eps) { keep[idx] = 1; rdp_keep_cc(in, i0, idx, eps, keep); rdp_keep_cc(in, idx, i1, eps, keep); }
}
}  // namespace detail

// Thin a captured controller stream down to breakpoints. A knob or wheel sends ~100 messages a
// second; a four-bar sweep is well over a thousand points, and storing them raw would bloat the
// project file and make the lane miserable to edit. Drops any point within `eps_v` of the linear
// interpolation of its kept neighbours, then enforces a minimum spacing so a jittery pot cannot
// produce a dense cloud of near-duplicates.
//
// Deliberately a sibling of decimate_curve rather than a call into it: different breakpoint type
// (double beats vs normalized float), and the `min_dt` floor has no reason to exist there.
inline std::vector<CcBp> decimate_cc(std::vector<CcBp> in, float eps_v, double min_dt) {
    std::sort(in.begin(), in.end(), [](const CcBp& a, const CcBp& b) { return a.t < b.t; });
    if (in.size() > 2) {
        std::vector<char> keep(in.size(), 0);
        keep.front() = 1; keep.back() = 1;
        detail::rdp_keep_cc(in, 0, in.size() - 1, eps_v, keep);
        std::vector<CcBp> thinned;
        thinned.reserve(in.size());
        for (size_t i = 0; i < in.size(); ++i) if (keep[i]) thinned.push_back(in[i]);
        in.swap(thinned);
    }
    if (min_dt <= 0.0 || in.size() <= 2) return in;
    std::vector<CcBp> out;
    out.reserve(in.size());
    out.push_back(in.front());
    for (size_t i = 1; i + 1 < in.size(); ++i)
        if (in[i].t - out.back().t >= min_dt) out.push_back(in[i]);
    out.push_back(in.back());          // the endpoint always survives, whatever the spacing
    return out;
}

// Sensible capture defaults: ~1 MIDI step of 127, and a 1/96-note floor.
constexpr float  kCcDecimateEps   = 0.008f;
constexpr double kCcDecimateMinDt = 1.0 / 96.0;

}  // namespace vivid::session
