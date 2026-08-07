#pragma once

#include <cmath>
#include <cstring>
#include <string>
#include <vector>
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
// v4 (ADR-0033 P5): added graph sticky notes (jg["annotations"]) + per-node labels (chain "name").
// Both are purely additive and read back with defaults, so a v3 file loads unchanged (absent ⇒ none).
// v5 (ADR-0051 P1): a DIRECTIONAL Light3D used to take its direction from `pos_*` and ignore `dir_*`
// entirely; now `dir_*` is the aim for every light type that has one. A pre-v5 directional light
// therefore has its intent in the WRONG field, and reading it as-is would swing every key light in
// every existing project. Loading one moves it across (see migrate_node_params).
constexpr int kSessionSchemaVersion = 5;

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

// A migration that has to see a node's params TOGETHER — migrate_param_value above is handed one
// name/value at a time and so cannot move a value from one param to another. Rewrites the params
// object in place, before the per-param loop reads it. Pure and total (an absent or malformed
// param is left alone), so it is unit-testable without a Session.
//
// v5 (ADR-0051 P1): a DIRECTIONAL Light3D took its direction from the translation column, i.e.
// from `pos_*`, and ignored `dir_*`. Now `dir_*` is the aim for every light type that has one.
// The uniform carries the vector pointing TOWARD the light, which the renderer derives as -aim,
// so the old `normalize(pos)` and the new `-normalize(dir)` agree exactly when dir = -pos. Move
// the value across with that negation and zero the now-meaningless position, and a pre-v5
// directional light keeps pointing where it always did.
inline void migrate_node_params(int file_ver, const std::string& op_type,
                                nlohmann::json& params) {
    if (!params.is_object()) return;

    if (file_ver < 5 && op_type == "Light3D") {
        auto num = [&params](const char* k, float dflt) {
            auto it = params.find(k);
            return (it != params.end() && it->is_number()) ? it->get<float>() : dflt;
        };
        // Only DIRECTIONAL lights changed meaning; point/spot already used pos_*/dir_* this way.
        // The default type is 0 (Directional), so an absent `type` must migrate too.
        if (static_cast<int>(std::lround(num("type", 0.f))) == 0) {
            const float px = num("pos_x", 0.5f);   // the pre-v5 Light3D pos_* defaults, which
            const float py = num("pos_y", 1.0f);   // doubled as the default direction
            const float pz = num("pos_z", 0.8f);
            const float len = std::sqrt(px*px + py*py + pz*pz);
            if (len > 1e-8f) {
                params["dir_x"] = -px / len;
                params["dir_y"] = -py / len;
                params["dir_z"] = -pz / len;
            }   // a degenerate pre-v5 position rendered as an unlit NaN; leave dir_* at its default
            params["pos_x"] = 0.0f;
            params["pos_y"] = 0.0f;
            params["pos_z"] = 0.0f;
        }
    }
}

// In-memory session <-> JSON: window + splitter/dock, per-track gain/active +
// MIDI clip notes (or audio trims) + FX chain, and the node graph (data nodes,
// op chain + base params, mappings, view). The track set itself is NOT persisted
// — it's rebuilt deterministically at startup (role-based), so load restores
// state onto the existing tracks by index. These power both file save/load and
// the MCP get_session/load_session tools.
// `include_plugin_state`: when false, the (potentially slow, RT-unsafe) VST3/CLAP `getState()`
// calls are SKIPPED. The undo/dirty canonical projection strips plugin state anyway
// (persist_undo.*), so it passes false — which also stops `getState()` from being called on a
// live, processing plugin during a reload's undo-baseline snapshot (a data race that SIGSEGV'd
// heavy plugins). Real file saves pass true.
nlohmann::json session_to_json(vivid::session::Session* s, vivid::ui::NodeGraph& g,
                               int win_w, int win_h, float split_x, float dock_h,
                               bool include_plugin_state = true);
// `base_dir` (the project folder) resolves project-RELATIVE audio media (`src_path`) at load — the
// audio peer of the visuals' asset_dir. Empty (the default, e.g. undo snapshots whose paths are
// already absolute) leaves paths untouched. Bundle-relative example media relies on it.
bool session_from_json(const nlohmann::json& j, vivid::session::Session* s, vivid::ui::NodeGraph& g,
                       int& win_w, int& win_h, float& split_x, float& dock_h,
                       const std::string& base_dir = "");

// ADR-0017 — how much of the audio session a restore rebuilds. Undo/redo restore the visual graph +
// mappings + pool unconditionally (cheap), but tier the expensive track/plugin work:
//   Skip       — the tracks block is unchanged; touch nothing audio (no plugin re-instantiation).
//   ParamsOnly — same track topology, only values differ; apply values without a rebuild (G3).
//   Full       — track topology differs; the full rebuild_tracks_from_doc path (the default).
enum class RestoreAudio { Skip, ParamsOnly, Full };
bool session_from_json_scoped(const nlohmann::json& j, vivid::session::Session* s, vivid::ui::NodeGraph& g,
                              int& win_w, int& win_h, float& split_x, float& dock_h, RestoreAudio audio,
                              const std::string& base_dir = "");

// File wrappers over the above. The audio-graph view (ADR-0023 step 6b) rides only on the file
// path — like the visual view it is UI/view state, kept out of the MCP document and undo (the
// canonical projection strips the visual view too). The audio camera is now an absolute NodeView
// (ADR-0023), persisted as {ox,oy,scale} just like the visual `view` block. On load, `ag_scale` is
// left untouched when the block is absent OR in the pre-migration `{zoom,pan_x,pan_y}` form — so the
// caller (which seeds `ag_scale = 0`) can tell "no camera loaded" and let the graph reset to fitted.
bool save_session(const std::string& path, vivid::session::Session* s, vivid::ui::NodeGraph& g,
                  int win_w, int win_h, float split_x, float dock_h,
                  float ag_ox, float ag_oy, float ag_scale);
bool load_session(const std::string& path, vivid::session::Session* s, vivid::ui::NodeGraph& g,
                  int& win_w, int& win_h, float& split_x, float& dock_h,
                  float& ag_ox, float& ag_oy, float& ag_scale);

// ADR-0033 P2b — audio copy/paste. capture_audio_nodes serializes the given track nodes (by stable id)
// + their internal edges to the persist JSON shape (skipping engine-managed kinds); paste_audio_subgraph
// appends such a clip onto a live track with FRESH gnids at (dx,dy) and returns the new local node ids.
// Together they implement duplicate (capture+paste same track) and clipboard copy/paste (incl. cross-track).
nlohmann::json capture_audio_nodes(vivid::session::Session* s, int track, const std::vector<int>& node_ids);
std::vector<int> paste_audio_subgraph(vivid::session::Session* s, int track, const nlohmann::json& clip,
                                      float dx, float dy);

}  // namespace vivid
