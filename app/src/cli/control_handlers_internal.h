#pragma once
// Shared implementation helpers for the control_handlers_*.cpp family files (audit #7). Included
// ONLY by those .cpp files — NOT by control_server.cpp, so its slim dispatch never sees these
// namespace-vivid helpers (avoids clashing with anything it defines).
#include "cli/control_handlers.h"
#include "cli/control_errors.h"   // ok/err/code (return json, so this pulls in nlohmann::json)
#include "cli/control_parse.h"    // in_range, char_id_from_source
#include "audio/vst3_host.h"      // session_* (for the index validators below)

#include <string>

namespace vivid {

using nlohmann::json;
using control::ok;
using control::err;
using control::in_range;
using control::char_id_from_source;
namespace code = control::code;

// Index validation against the live session (shared by handler families). On failure fills `e`
// with a stable out_of_range error and returns false, so handlers report the truth instead of
// silently no-op'ing.
inline bool need_track(vivid::session::Session* s, int t, json& e) {
    const int n = vivid::session::session_track_count(s);
    if (in_range(t, n)) return true;
    e = err(code::kOutOfRange, "track " + std::to_string(t) + " out of range [0," + std::to_string(n) + ")");
    return false;
}
inline bool need_scene(vivid::session::Session* s, int sc, json& e) {
    const int n = vivid::session::session_scene_count(s);
    if (in_range(sc, n)) return true;
    e = err(code::kOutOfRange, "scene " + std::to_string(sc) + " out of range [0," + std::to_string(n) + ")");
    return false;
}

}  // namespace vivid
