#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace vivid::ui {

// One operator's position in the semantic map plus enough metadata for the
// Map-tab preview column. Coordinates are in [0, 1] (normalized by the
// generator); the UI rescales them into panel space. Fields beyond {x, y,
// kind} are optional — a JSON written by an older generator will simply
// leave them empty and the preview falls back to name + kind.
struct OperatorLayoutEntry {
    float x = 0.5f;
    float y = 0.5f;
    std::string kind;           // "gpu" | "audio" | "control" | ""
    std::string hash;           // content hash of the text blob used to embed
    std::string brief;          // one-line description from @brief
    std::string multiplicity_behavior;  // "pointwise" | "structural" | ...
    int num_inputs = 0;
    int num_outputs = 0;
    std::vector<std::string> related;  // @see cross-references
};

// Immutable map of operator-type → 2D layout entry, sourced from the bundled
// operator_embeddings.json plus an optional user cache file written by the
// launch-time sidecar. Fallback lookups return nullptr so callers can handle
// "unknown operator" (typically: newly installed package) explicitly.
class OperatorLayout {
public:
    // Loads from <resources_dir>/operator_embeddings.json, then overlays any
    // entries present in <config_dir>/cache/operator_layout.json if that
    // file exists. Either path may be empty; a missing file is not an error.
    // Returns true if at least one operator was loaded.
    bool load(const std::string& resources_dir, const std::string& config_dir);

    const OperatorLayoutEntry* find(const std::string& type) const;

    const std::unordered_map<std::string, OperatorLayoutEntry>& all() const {
        return entries_;
    }

    size_t size() const { return entries_.size(); }
    bool empty() const { return entries_.empty(); }

private:
    bool load_file(const std::string& path);

    std::unordered_map<std::string, OperatorLayoutEntry> entries_;
};

}  // namespace vivid::ui
