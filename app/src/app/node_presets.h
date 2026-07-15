#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// ADR-0021/P4 — node presets: named snapshots of a node's params. Unlike the plugin preset flow
// (list_presets/load_preset), which loads opaque per-instrument binary state, a node preset is a
// param name->value (+ file/text param) JSON. Persisted by NAME so a shader header edit that adds
// or removes a param does not scramble the mapping. Stored tiered (user-writable > bundled factory),
// the same shape as the shader search path.
namespace vivid { namespace ui { class NodeGraph; } }

namespace vivid::node_presets {

// Snapshot node `idx`'s current params as { "params": {name: float}, "file_params": {name: str} }.
nlohmann::json capture(const ui::NodeGraph& g, int idx);

// Apply a captured preset to node `idx`: set every param the node HAS by name; silently skip names
// the node no longer has (a header edit may add/remove params). Returns the count applied.
int apply(ui::NodeGraph& g, int idx, const nlohmann::json& preset);

// The writable user directory for an op type's presets (created on demand). Empty on failure.
std::string user_presets_dir(const std::string& op_type);

struct PresetInfo { std::string name; std::string path; bool factory = false; };

// User + factory presets for `op_type`, sorted by name; a user preset shadows a factory one of the
// same name (user wins, listed once).
std::vector<PresetInfo> list(const std::string& op_type);

// Save `data` (a capture()) under `name` into the user dir. Returns the path written, or "" with
// `err` set (bad name, or write failure).
std::string save(const std::string& op_type, const std::string& name,
                 const nlohmann::json& data, std::string& err);

// Load a preset's JSON by name (user tier first, then factory). Null json when not found.
nlohmann::json load(const std::string& op_type, const std::string& name);

// Delete a USER preset by name. Returns false if it did not exist there (factory presets are
// read-only and never deleted).
bool remove(const std::string& op_type, const std::string& name);

}  // namespace vivid::node_presets
