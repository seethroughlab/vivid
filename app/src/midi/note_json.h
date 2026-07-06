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

}  // namespace vivid::session
