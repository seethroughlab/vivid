#pragma once

#include <string>
#include <vector>

// ADR-0021 / P2 — the bundled example projects. Each is a folder project (a directory with a
// project.json) that project_io::load opens like any other. This just finds them so the File
// menu can list them; opening one reuses file_actions::open_recent (open-by-path, no dialog).
namespace vivid::examples {

struct Example {
    std::string name;   // the directory basename (e.g. "drift"), shown in the menu
    std::string path;   // the project directory (a folder project)
};

// The directories where bundled examples live, in precedence order:
//   $VIVID_EXAMPLES_DIR (dev override)  >  <exe>/../Resources/examples  >  <exe>/examples
std::vector<std::string> examples_search_path();

// Pure: every immediate subdirectory of `dir` that contains a project.json, sorted by name so the
// menu reads the same on every machine. Missing/!dir returns empty (never throws).
std::vector<Example> discover_examples_in(const std::string& dir);

// Walk the search path; the first directory that yields any examples wins (one bundle, no merge).
std::vector<Example> discover_examples();

}  // namespace vivid::examples
