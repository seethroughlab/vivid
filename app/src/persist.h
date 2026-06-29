#pragma once
#include <string>
#include <nlohmann/json.hpp>

namespace vivid_poc { struct Session; }
namespace vivid::ui { class NodeGraph; }

namespace vivid {

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
