#pragma once
#include <string>
#include <nlohmann/json.hpp>

namespace vivid_poc { struct Session; }
namespace vivid::ui { class NodeGraph; }

namespace vivid {

// Session JSON schema version. Bump when the on-disk shape changes such that an OLDER
// reader couldn't safely parse a NEWER file. Load policy (classify_session_version):
//   equal  -> read as-is
//   older  -> best-effort read (every field is individually optional + migrated, e.g.
//             legacy_vop_name) — Migrated
//   newer  -> REFUSE (a newer Vivid wrote it; reading it would silently drop structure
//             we don't understand) — TooNew. This closes the old silent-accept gap.
constexpr int kSessionSchemaVersion = 1;

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

// In-memory session <-> JSON: window + splitter/dock, per-track gain/active +
// MIDI clip notes (or audio trims) + FX chain, and the node graph (data nodes,
// op chain + base params, mappings, view). The track set itself is NOT persisted
// — it's rebuilt deterministically at startup (role-based), so load restores
// state onto the existing tracks by index. These power both file save/load and
// the MCP get_session/load_session tools.
nlohmann::json session_to_json(vivid_poc::Session* s, vivid::ui::NodeGraph& g,
                               int win_w, int win_h, float split_x, float dock_h);
bool session_from_json(const nlohmann::json& j, vivid_poc::Session* s, vivid::ui::NodeGraph& g,
                       int& win_w, int& win_h, float& split_x, float& dock_h);

// File wrappers over the above.
bool save_session(const std::string& path, vivid_poc::Session* s, vivid::ui::NodeGraph& g,
                  int win_w, int win_h, float split_x, float dock_h);
bool load_session(const std::string& path, vivid_poc::Session* s, vivid::ui::NodeGraph& g,
                  int& win_w, int& win_h, float& split_x, float& dock_h);

}  // namespace vivid
