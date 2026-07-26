#pragma once
// The audio→visual bridge SOURCE-ID grammar, in one place. A "source id" is the string key the visuals
// graph resolves (DataNode.source / MappingRegistry) to a live scalar. The frame-side PUBLISHER
// (frame.cpp) and the Tab/right-click CATALOG builders (input.cpp) must emit BYTE-IDENTICAL ids for the
// same source or a wired node silently reads nothing — so the grammar lives here, built once, rather
// than being open-coded (and drifting) in each caller.
//
// Grammar:
//   master.<kind>  |  master.fft.<k>                 — the master bus (kinds 0..4 only; no notes)
//   track_<stableId>.<kind>  |  track_<id>.fft.<k>   — a track by STABLE id (kinds 0..7; 5/6/7 = note/vel/gate)
//   node_<stableTrackId>_<nodeId>.<rms|fft.k|ctl>    — one audio-graph node's output
#include <string>

namespace vivid::bridge {

// Per-track scalar source kinds, indexed 0..7. Kinds 5/6/7 (note/velocity/gate) are track-only; the
// master bus exposes only 0..4. The order matches the char_id kind encoding (100 + track*8 + kind).
inline constexpr int kNumTrackKinds = 8;
inline const char* const kTrackKindLabels[kNumTrackKinds] =
    { "Level", "Transient", "Low", "Mid", "High", "Note", "Velocity", "Gate" };
inline const char* const kTrackKindSuffixes[kNumTrackKinds] =
    { "level", "transient", "low", "mid", "high", "note", "velocity", "gate" };

// Prefixes (append ".fft.<k>" for a spectrum band, ".rms"/".ctl" for a node).
inline std::string master_prefix()                     { return "master"; }
inline std::string track_prefix(int track_id)          { return "track_" + std::to_string(track_id); }
inline std::string node_prefix(int track_id, int node_id) {
    return "node_" + std::to_string(track_id) + "_" + std::to_string(node_id);
}

// Full ids.
inline std::string master_source(const char* kind)    { return master_prefix() + "." + kind; }
inline std::string track_source(int track_id, const char* kind) { return track_prefix(track_id) + "." + kind; }
inline std::string track_fft(int track_id, int band)  { return track_prefix(track_id) + ".fft." + std::to_string(band); }
inline std::string master_fft(int band)               { return master_prefix() + ".fft." + std::to_string(band); }

}  // namespace vivid::bridge
