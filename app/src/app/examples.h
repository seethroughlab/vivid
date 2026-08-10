#pragma once

#include <string>
#include <vector>

// ADR-0021 / P2 — the bundled example projects. Each is a folder project (a directory with a
// project.json) that project_io::load opens like any other. This just finds them so the File
// menu can list them; opening one reuses file_actions::open_recent (open-by-path, no dialog).
namespace vivid::examples {

struct Example {
    std::string name;    // the directory basename (e.g. "drift"), shown in the menu
    std::string path;    // the project directory (a folder project)
    std::string group;   // "" = top-level menu item; else a submenu name (the parent dir, e.g.
                         // "operators" -> an "Operators" submenu). ADR-0054: per-operator examples
                         // live under examples/operators/ and group into their own submenu so they
                         // don't drown the curated demos.
};

// The directories where bundled examples live, in precedence order:
//   $VIVID_EXAMPLES_DIR (dev override)  >  <exe>/../Resources/examples  >  <exe>/examples
std::vector<std::string> examples_search_path();

// Pure, two-level: every immediate subdir of `dir` that contains a project.json is an ungrouped
// example (group=""); an immediate subdir that has NO project.json of its own but contains example
// subdirs is a GROUP, and its children are examples tagged with that group. Sorted by (group, name)
// so the menu reads the same on every machine. Missing/!dir returns empty (never throws).
std::vector<Example> discover_examples_in(const std::string& dir);

// Walk the search path; the first directory that yields any examples wins (one bundle, no merge).
std::vector<Example> discover_examples();

// If `project_dir` carries a project-local operator package (a `vivid-package.json`), return a
// WRITABLE copy under <user_data_dir>/example-cache/<basename> — so load_project can compile the
// package even when the source is a READ-ONLY app bundle (opening compiles the op INTO the folder).
// Otherwise (no package) returns `project_dir` unchanged. Empty string on copy failure.
std::string stage_openable_example(const std::string& project_dir);

}  // namespace vivid::examples
