#pragma once

#include <cmath>
#include <cstring>
#include <string>
#include <string>
#include <nlohmann/json.hpp>

namespace vivid::session { struct Session; }
namespace vivid::ui { class NodeGraph; }

namespace vivid {

// Session JSON schema version. Bump when the on-disk shape changes such that an OLDER
// reader couldn't safely parse a NEWER file. Load policy (classify_session_version):
//   equal  -> read as-is
//   older  -> best-effort read (every field is individually optional + migrated, e.g.
//             legacy_vop_name) — Migrated
//   newer  -> REFUSE (a newer Vivid wrote it; reading it would silently drop structure
//             we don't understand) — TooNew. This closes the old silent-accept gap.
// v2 (dynamic tracks): the track SET became part of the document (each track carries its
// kind + instrument), so load rebuilds the tracks; a v1 file restores onto the pre-built
// role set by index (migration).
// v3 (ADR-0016 / S5c): Composite's `mode` became a REAL ENUM (an int 0..4). It used to be a
// bare float 0..1 that the shader multiplied by 4 — so a pre-v3 file's 0.25 means "add", not
// "normal". Loading one rescales that value (see session_from_json); without this, every project
// that ever picked a blend mode would silently render a different one.
constexpr int kSessionSchemaVersion = 3;

enum class SessionVersionStatus { Ok, Migrated, TooNew };

// Classify a session document's "version" against kSessionSchemaVersion. A missing
// "version" is treated as the v1 baseline (pre-versioned files). Pure (no Session
// needed), so the loader + the headless test share one implementation.
inline SessionVersionStatus classify_session_version(const nlohmann::json& j,
                                                     int* out_file_version = nullptr) {
    const int v = j.is_object() ? j.value("version", 1) : 1;
    if (out_file_version) *out_file_version = v;
    if (v > kSessionSchemaVersion) return SessionVersionStatus::TooNew;
    if (v < kSessionSchemaVersion) return SessionVersionStatus::Migrated;
    return SessionVersionStatus::Ok;
}

// Pre-P1 sessions stored a visual node's op as the legacy VOp enum int ("op").
// Map it to the operator type name ("op_type") for loading. Out-of-range -> Plasma.
inline const char* legacy_vop_name(int op) {
    static const char* kNames[] = { "Plasma", "Video", "Feedback", "Blur", "Output" };
    return kNames[(op >= 0 && op < 5) ? op : 0];
}

// A saved param VALUE whose MEANING changed between schema versions. Pure, so the rule is
// testable on its own (like legacy_vop_name above) rather than buried in the load path.
//
// v3 (ADR-0016 / S5c): Composite's `mode` used to be a bare float 0..1 that the shader scaled
// by 4 to pick one of five blend modes; it is now a real enum index (0..4). A pre-v3 file's
// 0.25 therefore means "add" — read as an index it would be 0, "normal", and every project
// that ever chose a blend mode would quietly render a different one.
inline float migrate_param_value(int file_ver, const std::string& op_type, const char* name,
                                 float v) {
    if (file_ver < 3 && op_type == "Composite" && name && std::strcmp(name, "mode") == 0)
        return std::round(v * 4.f);
    return v;
}

// In-memory session <-> JSON: window + splitter/dock, per-track gain/active +
// MIDI clip notes (or audio trims) + FX chain, and the node graph (data nodes,
// op chain + base params, mappings, view). The track set itself is NOT persisted
// — it's rebuilt deterministically at startup (role-based), so load restores
// state onto the existing tracks by index. These power both file save/load and
// the MCP get_session/load_session tools.
nlohmann::json session_to_json(vivid::session::Session* s, vivid::ui::NodeGraph& g,
                               int win_w, int win_h, float split_x, float dock_h);
bool session_from_json(const nlohmann::json& j, vivid::session::Session* s, vivid::ui::NodeGraph& g,
                       int& win_w, int& win_h, float& split_x, float& dock_h);

// ADR-0017 — how much of the audio session a restore rebuilds. Undo/redo restore the visual graph +
// mappings + pool unconditionally (cheap), but tier the expensive track/plugin work:
//   Skip       — the tracks block is unchanged; touch nothing audio (no plugin re-instantiation).
//   ParamsOnly — same track topology, only values differ; apply values without a rebuild (G3).
//   Full       — track topology differs; the full rebuild_tracks_from_doc path (the default).
enum class RestoreAudio { Skip, ParamsOnly, Full };
bool session_from_json_scoped(const nlohmann::json& j, vivid::session::Session* s, vivid::ui::NodeGraph& g,
                              int& win_w, int& win_h, float& split_x, float& dock_h, RestoreAudio audio);

// File wrappers over the above. The audio-graph view (ADR-0023 step 6b) rides only on the file
// path — like the visual view it is UI/view state, kept out of the MCP document and undo (the
// canonical projection strips the visual view too). On load, ag_* default to the passed-in values
// when the file omits the block, so a pre-6b session keeps the fitted view.
bool save_session(const std::string& path, vivid::session::Session* s, vivid::ui::NodeGraph& g,
                  int win_w, int win_h, float split_x, float dock_h,
                  float ag_zoom, float ag_pan_x, float ag_pan_y);
bool load_session(const std::string& path, vivid::session::Session* s, vivid::ui::NodeGraph& g,
                  int& win_w, int& win_h, float& split_x, float& dock_h,
                  float& ag_zoom, float& ag_pan_x, float& ag_pan_y);

}  // namespace vivid
