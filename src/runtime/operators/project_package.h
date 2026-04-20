#pragma once

#include <string>
#include <utility>

namespace vivid {

class Graph;
class PackageManager;

// Find or create the project's local-operators package beside the graph file.
//
// If a workspace project package is already registered with the package
// manager, returns its {root_path, name}. Otherwise scaffolds a new
// `<graph_dir>/operators/` package (with vivid-package.json + CMakeLists.txt
// + empty src/), links it, and returns the new {root_path, name}.
//
// Returns {"", ""} if the graph has no saved source path, or if package
// scaffolding/linking fails. Diagnostics go to stderr.
//
// Used by both the "clone-to-edit" command sink path and the
// scaffold_operator dispatch handler so both honor the same per-project
// destination policy.
std::pair<std::string, std::string> ensure_project_package(
    PackageManager& mgr, const Graph& graph);

} // namespace vivid
