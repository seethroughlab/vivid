#pragma once
#include <string>

namespace vivid_poc { struct Session; }
namespace vivid::ui { class NodeGraph; }

namespace vivid {

// Save/load a whole session to JSON: window + splitter, per-track gain/active +
// MIDI clip notes (or audio trims), and the node graph (nodes + wiring). The
// track set itself is NOT persisted — it's rebuilt deterministically at startup
// (role-based), so load restores state onto the existing tracks by index.
bool save_session(const std::string& path, vivid_poc::Session* s, vivid::ui::NodeGraph& g,
                  int win_w, int win_h, float split_x, float dock_h);
bool load_session(const std::string& path, vivid_poc::Session* s, vivid::ui::NodeGraph& g,
                  int& win_w, int& win_h, float& split_x, float& dock_h);

}  // namespace vivid
