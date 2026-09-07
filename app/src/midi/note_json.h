#pragma once
#include "midi/midi_clip.h"   // vivid::session::ClipNote / ExprCurve / AXIS_*
#include <nlohmann/json.hpp>

// JSON (de)serialization for per-note expression curves (M3). Shared by the control
// server and session persistence so the wire + file formats agree. Each axis is an
// optional array of [t, v] pairs (t = normalized time 0..1; v in axis units: bend =
// semitones, pressure/timbre 0..1). Absent axis = flat (back-compatible with old clips).
namespace vivid::session {

inline const char* const* expr_axis_keys() {
    static const char* keys[AXIS_COUNT] = { "bend", "pressure", "timbre" };
    return keys;
}

// Write any non-empty expression curves of `n` into the note object `jn`.
inline void expr_to_json(const ClipNote& n, nlohmann::json& jn) {
    const char* const* keys = expr_axis_keys();
    for (int a = 0; a < AXIS_COUNT; ++a) {
        if (n.expr[a].empty()) continue;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& bp : n.expr[a].bp) arr.push_back({ bp.t, bp.v });
        jn[keys[a]] = arr;
    }
}

// Parse expression curves from the note object `jn` into `n` (leaves axes absent from
// the JSON untouched / empty). Skips malformed points defensively.
inline void expr_from_json(const nlohmann::json& jn, ClipNote& n) {
    const char* const* keys = expr_axis_keys();
    for (int a = 0; a < AXIS_COUNT; ++a) {
        if (!jn.contains(keys[a]) || !jn[keys[a]].is_array()) continue;
        n.expr[a].bp.clear();
        for (const auto& p : jn[keys[a]])
            if (p.is_array() && p.size() >= 2)
                n.expr[a].bp.push_back({ p[0].get<float>(), p[1].get<float>() });
    }
}

// --- Clip-level controller lanes (P4) ---
// Same posture as the per-note axes above: an optional key, absent = none, malformed points
// skipped defensively. Shape:  "cc": [ { "n": 1, "ch": 0, "pts": [[beat, v], ...] }, ... ]
// `n` is the ControllerNumbers value (0..127 CC, 128 channel pressure, 129 pitch bend) and `v` is
// normalized 0..1. Purely additive, so no session-schema bump: an older reader ignores the key and
// loads the notes byte-identically (the cost, as with every additive field, is that re-saving from
// such a build drops the lanes).
inline void cc_to_json(const MidiClip& c, nlohmann::json& jc) {
    if (c.cc.empty()) return;
    nlohmann::json lanes = nlohmann::json::array();
    for (const CcLane& lane : c.cc) {
        if (lane.empty()) continue;
        nlohmann::json pts = nlohmann::json::array();
        for (const CcBp& p : lane.bp) pts.push_back({ p.t, p.v });
        lanes.push_back({ {"n", lane.cc}, {"ch", lane.channel}, {"pts", pts} });
    }
    if (!lanes.empty()) jc["cc"] = lanes;
}

inline void cc_from_json(const nlohmann::json& jc, MidiClip& c) {
    c.cc.clear();
    if (!jc.contains("cc") || !jc["cc"].is_array()) return;
    for (const auto& jl : jc["cc"]) {
        if (!jl.is_object() || !jl.contains("pts") || !jl["pts"].is_array()) continue;
        const int n = jl.value("n", -1);
        if (n < 0 || n >= kCcCount) continue;              // out of the controller space: drop
        CcLane lane;
        lane.cc      = static_cast<uint16_t>(n);
        lane.channel = static_cast<uint8_t>(jl.value("ch", 0) & 0x0F);
        for (const auto& p : jl["pts"])
            if (p.is_array() && p.size() >= 2)
                lane.bp.push_back({ p[0].get<double>(), std::clamp(p[1].get<float>(), 0.f, 1.f) });
        std::sort(lane.bp.begin(), lane.bp.end(),
                  [](const CcBp& a, const CcBp& b) { return a.t < b.t; });   // sample() requires sorted
        if (!lane.bp.empty() && c.cc.size() < static_cast<size_t>(kMaxCcLanes))
            c.cc.push_back(std::move(lane));
    }
}

}  // namespace vivid::session
